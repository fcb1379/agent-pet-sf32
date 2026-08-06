$ErrorActionPreference = "Stop"

function Find-Sftool {
    # 1. PATH 中的 sftool
    $command = Get-Command "sftool" -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    # 2. SDK 安装目录 (sdk\install.ps1 下载)
    $toolRoot = Join-Path $env:USERPROFILE ".sifli\tools\sftool"
    if (Test-Path $toolRoot) {
        $exe = Get-ChildItem -Path $toolRoot -Recurse -Filter "sftool.exe" -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($null -ne $exe) {
            return $exe.FullName
        }
    }

    return $null
}

function Get-AvailableComPorts {
    # 优先用 PnP 查询, 返回带设备名的列表, 如 "COM5 (USB-SERIAL CH340)"
    $list = @()
    try {
        $devices = Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '\((COM\d+)\)' }
        foreach ($device in $devices) {
            $portMatch = [regex]::Match($device.Name, '\((COM\d+)\)')
            if ($portMatch.Success) {
                $deviceName = ($device.Name -replace ' \((COM\d+)\)$', '')
                $list += "$($portMatch.Groups[1].Value) ($deviceName)"
            }
        }
    }
    catch {
        # fall through to SerialPort fallback below
    }

    if ($list.Count -eq 0) {
        # 回退: 仅端口号列表
        try {
            $list = @([System.IO.Ports.SerialPort]::GetPortNames())
        }
        catch {
            $list = @()
        }
    }

    return $list
}

function Invoke-FirmwareFlash {
    param(
        [string]$SftoolPath,
        [string]$Port,
        [string]$BuildOutputDir
    )

    if (-not (Test-Path $SftoolPath)) {
        throw "sftool not found: $SftoolPath"
    }

    # 烧录前检查产物存在
    $requiredFiles = @(
        "bootloader\bootloader.bin",
        "main.bin",
        "ftab\ftab.bin",
        "fs_root.bin"
    )
    foreach ($file in $requiredFiles) {
        if (-not (Test-Path (Join-Path $BuildOutputDir $file))) {
            throw "Flash artifact missing: $file (looked in $BuildOutputDir)"
        }
    }

    $flashCommand = @(
        "-p", "COM$Port",
        "-c", "SF32LB52",
        "write_flash",
        "bootloader\bootloader.bin@0x12010000",
        "main.bin@0x12020000",
        "ftab\ftab.bin@0x12000000",
        "fs_root.bin@0x129A0000"
    )

    Write-Host "Flashing firmware to COM$Port ..."
    Push-Location $BuildOutputDir
    try {
        & $SftoolPath @flashCommand
        $flashExitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }

    if (0 -ne $flashExitCode) {
        throw "Flash failed with exit code $flashExitCode. Check the board connection and port."
    }

    Write-Host "Flash completed successfully."
}

$projectRoot = $PSScriptRoot
$sdkRoot = Join-Path $projectRoot "sdk"
$sdkExport = Join-Path $sdkRoot "export.ps1"
$projectDirectory = Join-Path $projectRoot "work\watch_bt_audio_template\project"
$projectFile = Join-Path $projectDirectory "SConstruct"

try {
    if (-not (Test-Path -LiteralPath $sdkExport -PathType Leaf)) {
        throw "SiFli SDK is missing. Run: git submodule update --init --recursive"
    }

    if (-not (Test-Path -LiteralPath $projectFile -PathType Leaf)) {
        throw "Firmware project was not found: $projectFile"
    }

    $sconsCommand = Get-Command "scons" -ErrorAction SilentlyContinue
    $toolchainCommand = Get-Command "arm-none-eabi-gcc" -ErrorAction SilentlyContinue
    $environmentReady = ($null -ne $sconsCommand) -and
        ($null -ne $toolchainCommand) -and
        (-not [string]::IsNullOrWhiteSpace($env:SIFLI_SDK))

    if (-not $environmentReady) {
        if ($null -eq (Get-Command "python" -ErrorAction SilentlyContinue)) {
            throw "The SiFli environment is not ready and Python is unavailable. Start a SiFli ENV terminal, or install Python and run sdk\install.ps1 once."
        }

        Write-Host "Initializing SiFli SDK environment..."

        # Remove MSYS/Mingw markers inherited from git-bash shells; the SiFli
        # SDK rejects environments that still carry them.
        Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue

        # Ensure Python 3.9+ is found first (Zephyr SDK ships Python 3.8
        # which is older than what SiFli-SDK requires). Detect installed
        # Python executables from the registry, PATH, and common install
        # directories instead of guessing fixed paths.
        $pythonExes = @()

        # 1. 注册表 (HKCU + HKLM) 中 Python 安装记录
        foreach ($hive in 'HKCU:\Software\Python\PythonCore', 'HKLM:\Software\Python\PythonCore') {
            if (Test-Path $hive) {
                Get-ChildItem $hive -ErrorAction SilentlyContinue | ForEach-Object {
                    $installPath = Join-Path $_.PSPath 'InstallPath'
                    if (Test-Path $installPath) {
                        $dir = (Get-ItemProperty $installPath -ErrorAction SilentlyContinue).'(default)'
                        if ($dir -and (Test-Path "$dir\python.exe")) {
                            $pythonExes += "$dir\python.exe"
                        }
                    }
                }
            }
        }

        # 2. PATH 中所有 python*.exe
        Get-Command "python*" -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^python(?:3\.\d+)?\.exe$' } |
            ForEach-Object { $pythonExes += $_.Source }

        # 3. 常见安装目录 (通配 Python*)
        foreach ($base in @("$env:LOCALAPPDATA\Programs\Python", "$env:ProgramFiles\Python", "$env:ProgramFiles(x86)\Python", 'C:\Python')) {
            if (Test-Path $base) {
                Get-ChildItem $base -Directory -Filter 'Python*' -ErrorAction SilentlyContinue |
                    ForEach-Object {
                        if (Test-Path "$($_.FullName)\python.exe") {
                            $pythonExes += "$($_.FullName)\python.exe"
                        }
                    }
            }
        }

        # 排除 Microsoft Store stub (WindowsApps), 其版本无法验证且可能弹出商店
        $pythonExes = $pythonExes | Where-Object { $_ -notmatch 'WindowsApps' }

        # 去重并选择第一个 >= 3.9 的 Python, 前置到 PATH
        foreach ($candidate in ($pythonExes | Select-Object -Unique)) {
            if (-not (Test-Path $candidate -PathType Leaf)) {
                continue
            }
            try {
                $versionOutput = & $candidate -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')" 2>$null
                if ($versionOutput -match '^(\d+)\.(\d+)') {
                    $pythonMajor = [int]$Matches[1]
                    $pythonMinor = [int]$Matches[2]
                    if (($pythonMajor -gt 3) -or (($pythonMajor -eq 3) -and ($pythonMinor -ge 9))) {
                        $env:PATH = "$(Split-Path $candidate);$env:PATH"
                        Write-Host "Using Python $pythonMajor.$pythonMinor at $candidate"
                        break
                    }
                }
            }
            catch {
                # skip unreadable candidates
            }
        }

        Push-Location $sdkRoot

        try {
            . $sdkExport
        }
        finally {
            Pop-Location
        }
    }

    if ($null -eq (Get-Command "scons" -ErrorAction SilentlyContinue)) {
        throw "scons is unavailable after SDK initialization. Run sdk\install.ps1 once."
    }

    $sconsArguments = @(
        "--board=sf32lb52-lchspi-ulp"
        "--board_search_path=../boards"
    )

    # 分离 build/flash 参数: -flash 跳过询问, -port <n> 指定串口
    $autoFlash = $false
    $flashPort = $null
    $sconsArgs = @()
    for ($i = 0; $i -lt $args.Count; $i++) {
        $argument = $args[$i]
        if ($argument -eq "-flash") {
            $autoFlash = $true
        }
        elseif ($argument -eq "-port" -and ($i + 1) -lt $args.Count) {
            $flashPort = $args[$i + 1]
            $i++
        }
        else {
            $sconsArgs += $argument
        }
    }

    $hasParallelOption = $false
    foreach ($argument in $sconsArgs) {
        if (($argument -match "^-j[0-9]+$") -or
            ($argument -match "^--jobs(?:=|$)")) {
            $hasParallelOption = $true
            break
        }
    }

    if (-not $hasParallelOption) {
        $sconsArguments += "-j8"
    }

    $sconsArguments += $sconsArgs

    Write-Host "Building Agent Pet firmware for sf32lb52-lchspi-ulp..."
    Push-Location $projectDirectory

    try {
        & scons @sconsArguments
        $buildExitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }

    if (0 -ne $buildExitCode) {
        throw "Firmware build failed with exit code $buildExitCode."
    }

    Write-Host "Build completed successfully."
    Write-Host "Output: work\watch_bt_audio_template\project\build_sf32lb52-lchspi-ulp_hcpu"

    # 编译下载一体: 询问是否烧录, 确认后进入下载流程
    # 非交互环境 (CI/脚本, stdin 重定向) 默认不烧录, 仅 -flash 显式启用
    $shouldFlash = $autoFlash
    if (-not $shouldFlash -and -not [Console]::IsInputRedirected) {
        $answer = Read-Host "是否立即烧录到板子？(Y/N)"
        $shouldFlash = $answer -match "^[yY]$"
    }
    elseif (-not $shouldFlash) {
        Write-Host "非交互环境, 跳过烧录询问 (使用 -flash 可强制烧录)。"
    }

    if ($shouldFlash) {
        $sftoolPath = Find-Sftool
        if ($null -eq $sftoolPath) {
            Write-Warning "sftool 未找到，跳过烧录。请先运行 sdk\install.ps1 安装 SDK 工具。"
        }
        else {
            $buildOutputDir = Join-Path $projectDirectory "build_sf32lb52-lchspi-ulp_hcpu"
            $port = $flashPort
            $flashSucceeded = $false
            $flashAborted = $false

            # 端口选择 + 烧录循环: 输入错误或烧录失败可修正, 无需重新编译
            while (-not $flashSucceeded) {
                # 每次循环刷新可用串口列表, 按 COM 号排序保证默认值稳定
                $availablePorts = @(Get-AvailableComPorts | Sort-Object {
                    [int]([regex]::Match($_, 'COM(\d+)').Groups[1].Value)
                })
                $defaultPort = if ($availablePorts.Count -gt 0) {
                    ([regex]::Match($availablePorts[0], 'COM(\d+)')).Groups[1].Value
                }
                else {
                    "4"
                }

                Write-Host ""
                if ($availablePorts.Count -gt 0) {
                    Write-Host "可用串口: $($availablePorts -join ', ')"
                    $defaultDevice = ($availablePorts[0] -replace '^COM\d+ \(', '') -replace '\)$', ''
                    $defaultLabel = "$defaultPort ($defaultDevice)"
                }
                else {
                    Write-Warning "未检测到可用串口, 请检查板子 USB 连接后重试"
                    $defaultLabel = ""
                }

                # 已有 -port 参数且有效则跳过询问; 无效或未提供则重新输入
                # 端口输入兼容 "4" 和 "COM4" 两种格式
                $portValid = $port -match "^(?:COM)?(\d+)$" -and
                    (($availablePorts.Count -eq 0) -or
                     ($availablePorts -match "COM$($port -replace '[^0-9]', '')\b"))
                if (-not $portValid) {
                    if ($null -ne $port) {
                        Write-Warning "端口 COM$($port -replace '[^0-9]', '') 不存在, 请重新输入"
                    }
                    if ([Console]::IsInputRedirected) {
                        # 非交互环境: 无法询问, 要求显式 -port 参数
                        Write-Warning "非交互环境: 请使用 -port <n> 指定有效端口"
                        $flashAborted = $true
                        break
                    }
                    if ([string]::IsNullOrWhiteSpace($defaultLabel)) {
                        # 无可用串口: 不提供默认值, 输入 N/n 退出
                        $portInput = Read-Host "请输入串口号 (N 退出)"
                        if ($portInput -match "^(?:n|N)$") {
                            Write-Host "已跳过烧录。"
                            break
                        }
                        $port = $portInput.Trim()
                    }
                    else {
                        $portInput = Read-Host "请输入串口号 (默认 $defaultLabel)"
                        $port = if ([string]::IsNullOrWhiteSpace($portInput)) { $defaultPort } else { $portInput.Trim() }
                    }
                    $portValid = $port -match "^(?:COM)?(\d+)$"
                }

                if (-not $portValid) {
                    Write-Warning "端口号无效: $port (请输入数字或 COM 端口名)"
                    $port = $null
                    continue
                }

                # 统一为纯数字, 供 Invoke-FirmwareFlash 拼 COM$Port
                $port = $Matches[1]

                try {
                    Invoke-FirmwareFlash -SftoolPath $sftoolPath -Port $port -BuildOutputDir $buildOutputDir
                    $flashSucceeded = $true
                }
                catch {
                    Write-Warning $_.Exception.Message
                    if ([Console]::IsInputRedirected) {
                        break   # 非交互环境: 烧录失败即退出
                    }
                    $retryAnswer = Read-Host "烧录失败, 是否重试？(Y/N)"
                    if ($retryAnswer -notmatch "^[yY]$") {
                        break
                    }
                    $port = $null   # 重新选择串口
                }
            }

            if ($flashAborted) {
                throw "烧录未执行: 端口无效 (非交互环境请使用 -port <n> 指定有效端口)"
            }
        }
    }
}
catch {
    [Console]::Error.WriteLine("ERROR: " + $_.Exception.Message)
    exit 1
}
