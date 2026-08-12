# Build-and-flash helper functions shared by build.ps1 and its tests.
# This file contains only function definitions; dot-source it before use:
#   . "$PSScriptRoot\build-flash-utils.ps1"

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
        "ftab\ftab.bin"
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
        "ftab\ftab.bin@0x12000000"
    )


    # Normal updates never program fs_root at 0x129A0000.
    # Factory and legacy assets in fs_root therefore survive normal updates.
    # Custom mascot files live on the TF card, outside every flash target above.
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

# 端口选择 + 烧录循环。返回烧录是否成功; 失败会 throw:
#   - 非交互 (或 -flash 显式) 模式下, 端口无效或烧录失败都立即抛错, 保证非零退出
#   - 交互模式下, 用户主动取消 (输入 N) 返回 $false, 调用方可以正常结束
function Select-FlashPort {
    param(
        [string]$SftoolPath,
        [string]$BuildOutputDir,
        [string]$InitialPort,
        [bool]$Interactive = (-not [Console]::IsInputRedirected)
    )

    $port = $InitialPort
    $flashSucceeded = $false
    $flashAborted = $false
    $userCancelled = $false

    while (-not $flashSucceeded -and -not $userCancelled) {
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
            if (-not [string]::IsNullOrWhiteSpace($port)) {
                Write-Warning "端口 COM$($port -replace '[^0-9]', '') 不存在, 请重新输入"
            }
            if (-not $Interactive) {
                # 非交互环境: 无法询问, 要求显式 -port 参数
                Write-Warning "非交互环境: 请使用 -port <n> 指定有效端口"
                $flashAborted = $true
                break
            }
            # 输入 N/n 一律视为取消 (无论是否有可用串口)
            if ([string]::IsNullOrWhiteSpace($defaultLabel)) {
                $portInput = Read-Host "请输入串口号 (N 退出)"
            }
            else {
                $portInput = Read-Host "请输入串口号 (默认 $defaultLabel)"
            }
            if ($portInput -match "^(?:n|N)$") {
                Write-Host "已跳过烧录。"
                $userCancelled = $true
                break
            }
            $port = if ([string]::IsNullOrWhiteSpace($portInput)) { $defaultPort } else { $portInput.Trim() }
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
            Invoke-FirmwareFlash -SftoolPath $SftoolPath -Port $port -BuildOutputDir $BuildOutputDir
            $flashSucceeded = $true
        }
        catch {
            Write-Warning $_.Exception.Message
            if (-not $Interactive) {
                # 非交互环境: 烧录失败立即向外传播, 保证调用方非零退出
                throw
            }
            $retryAnswer = Read-Host "烧录失败, 是否重试？(Y/N)"
            if ($retryAnswer -notmatch "^[yY]$") {
                $userCancelled = $true
            }
            else {
                $port = $null   # 重新选择串口
            }
        }
    }

    if ($flashAborted) {
        throw "烧录未执行: 端口无效 (非交互环境请使用 -port <n> 指定有效端口)"
    }

    return $flashSucceeded
}
