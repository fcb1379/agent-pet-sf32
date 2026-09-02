$ErrorActionPreference = "Stop"

# Flash helpers live in a separate file so tests can dot-source and mock them.
. (Join-Path $PSScriptRoot "build-flash-utils.ps1")

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
        foreach ($base in @("$env:LOCALAPPDATA\Programs\Python", "$env:ProgramFiles\Python", "${env:ProgramFiles(x86)}\Python", 'C:\Python')) {
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
                $versionOutput = & $candidate -c "import platform, sys; print(f'{sys.version_info.major}.{sys.version_info.minor}|{platform.machine()}')" 2>$null
                if ($versionOutput -match '^(\d+)\.(\d+)\|(.+)$') {
                    $pythonMajor = [int]$Matches[1]
                    $pythonMinor = [int]$Matches[2]
                    $pythonArchitecture = $Matches[3]
                    if (($pythonMajor -gt 3) -or (($pythonMajor -eq 3) -and ($pythonMinor -ge 9))) {
                        $env:PATH = "$(Split-Path $candidate);$env:PATH"
                        Write-Host "Using Python $pythonMajor.$pythonMinor ($pythonArchitecture) at $candidate"
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
            # 显式/自动烧录模式下找不到 sftool 是失败, 必须非零退出;
            # 只有交互式用户主动选择跳过 (输入 N) 才可成功结束
            throw "sftool 未找到, 无法烧录。请先运行 sdk\install.ps1 安装 SDK 工具。"
        }

        $buildOutputDir = Join-Path $projectDirectory "build_sf32lb52-lchspi-ulp_hcpu"
        # 端口选择 + 烧录循环: 输入错误或烧录失败可修正, 无需重新编译。
        # 非交互/显式模式下的任何失败都会由 Select-FlashPort 抛出, 保证非零退出。
        [void](Select-FlashPort -SftoolPath $sftoolPath -BuildOutputDir $buildOutputDir -InitialPort $flashPort)
    }
}
catch {
    [Console]::Error.WriteLine("ERROR: " + $_.Exception.Message)
    exit 1
}
