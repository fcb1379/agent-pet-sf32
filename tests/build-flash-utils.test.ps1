# 回归测试: build-flash-utils.ps1 的烧录流程 (不依赖真实硬件, 全部 mock)
# 运行: powershell -NoProfile -ExecutionPolicy Bypass -File tests\build-flash-utils.test.ps1
# 预期输出: PASS build-flash-utils.test
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path $PSScriptRoot -Parent
. (Join-Path $repoRoot "build-flash-utils.ps1")

$failures = @()

function Assert-Equal {
    param($Expected, $Actual, $Message)
    if ($Expected -ne $Actual) {
        $script:failures += $Message
        Write-Host "FAIL: $Message (expected '$Expected', got '$Actual')"
    }
    else {
        Write-Host "PASS: $Message"
    }
}

function Assert-Throws {
    param([scriptblock]$ScriptBlock, $Message, [string]$Match = $null)
    try {
        & $ScriptBlock
        $script:failures += $Message
        Write-Host "FAIL: $Message (no exception thrown)"
    }
    catch {
        if ($Match -and $_.Exception.Message -notmatch $Match) {
            $script:failures += $Message
            Write-Host "FAIL: $Message (unexpected message: $($_.Exception.Message))"
        }
        else {
            Write-Host "PASS: $Message"
        }
    }
}

# --- 测试 1: 找不到 sftool 返回 null ---
Write-Host "== Test 1: Find-Sftool returns null when unavailable =="
$oldUserProfile = $env:USERPROFILE
$oldPath = $env:PATH
$isolated = Join-Path $env:TEMP ("aptest-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory $isolated -Force | Out-Null
$env:USERPROFILE = $isolated
$env:PATH = $isolated
$found = Find-Sftool
Assert-Equal $null $found "找不到 sftool 时返回 null"
$env:USERPROFILE = $oldUserProfile
$env:PATH = $oldPath

# --- 测试 2: 缺少烧录产物 ---
Write-Host "== Test 2: Invoke-FirmwareFlash throws on missing artifacts =="
$tempDir = Join-Path $env:TEMP ("aptest-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory (Join-Path $tempDir "bootloader") -Force | Out-Null
New-Item -ItemType Directory (Join-Path $tempDir "ftab") -Force | Out-Null
$fakeSftool = Join-Path $tempDir "fake-sftool.cmd"
"@echo off`r`nexit /b 7`r`n" | Set-Content -Path $fakeSftool -Encoding ASCII
# 产物缺失 (只有空目录)
Assert-Throws { Invoke-FirmwareFlash -SftoolPath $fakeSftool -Port 1 -BuildOutputDir $tempDir } "缺少烧录产物应抛错" "Flash artifact missing"

# --- 测试 3: sftool 返回非零 ---
Write-Host "== Test 3: Invoke-FirmwareFlash throws when sftool exits non-zero =="
Set-Content (Join-Path $tempDir "bootloader\bootloader.bin") "x" -Encoding ASCII
Set-Content (Join-Path $tempDir "main.bin") "x" -Encoding ASCII
Set-Content (Join-Path $tempDir "ftab\ftab.bin") "x" -Encoding ASCII
Set-Content (Join-Path $tempDir "fs_root.bin") "x" -Encoding ASCII
Assert-Throws { Invoke-FirmwareFlash -SftoolPath $fakeSftool -Port 1 -BuildOutputDir $tempDir } "sftool 非零退出应抛错" "Flash failed with exit code 7"

# --- 测试 4: 非交互无效端口 → 抛错 ---
Write-Host "== Test 4: Select-FlashPort throws on invalid port in non-interactive mode =="
function Get-AvailableComPorts { return @("COM3 (fake device)") }
Assert-Throws { Select-FlashPort -SftoolPath $fakeSftool -BuildOutputDir $tempDir -InitialPort 999 -Interactive $false } "非交互无效端口应抛错" "端口无效"

# --- 测试 5: 非交互烧录失败 → 异常传播 (关键回归: 此前会误报成功) ---
Write-Host "== Test 5: Select-FlashPort propagates flash failure in non-interactive mode =="
Assert-Throws { Select-FlashPort -SftoolPath $fakeSftool -BuildOutputDir $tempDir -InitialPort 3 -Interactive $false } "非交互烧录失败应抛错" "Flash failed"

# --- 测试 6: 交互端口询问输入 N → 主动取消, 不调用烧录 ---
Write-Host "== Test 6: interactive cancel at port prompt returns false without flashing =="
$script:flashCallCount = 0
function Read-Host { param([string]$Prompt) return "N" }
function Invoke-FirmwareFlash { param([string]$SftoolPath, [string]$Port, [string]$BuildOutputDir) $script:flashCallCount++; throw "should not be called" }
$result = Select-FlashPort -SftoolPath $fakeSftool -BuildOutputDir $tempDir -Interactive $true
Assert-Equal $false $result "交互端口询问输入 N 应返回 false 不抛错"
Assert-Equal 0 $script:flashCallCount "取消后不应调用烧录"

# --- 测试 7: 交互烧录失败后用户取消 ---
Write-Host "== Test 7: interactive cancel after flash failure returns false =="
function Invoke-FirmwareFlash { param([string]$SftoolPath, [string]$Port, [string]$BuildOutputDir) throw "flash failed" }
$result = Select-FlashPort -SftoolPath $fakeSftool -BuildOutputDir $tempDir -InitialPort 3 -Interactive $true
Assert-Equal $false $result "烧录失败后输入 N 应返回 false 不抛错"

# --- 测试 8: 交互烧录失败后重试 (Y) 再取消 (N) ---
Write-Host "== Test 8: interactive retry then cancel =="
$script:readHostAnswers = @("Y", "N")
$script:readHostIndex = 0
function Read-Host { param([string]$Prompt) $answer = $script:readHostAnswers[$script:readHostIndex]; $script:readHostIndex++; return $answer }
$result = Select-FlashPort -SftoolPath $fakeSftool -BuildOutputDir $tempDir -InitialPort 3 -Interactive $true
Assert-Equal $false $result "重试后取消应返回 false"

# --- 测试 9: 交互烧录成功 ---
Write-Host "== Test 9: successful flash returns true =="
function Invoke-FirmwareFlash { param([string]$SftoolPath, [string]$Port, [string]$BuildOutputDir) Write-Host "fake flash ok" }
$result = Select-FlashPort -SftoolPath $fakeSftool -BuildOutputDir $tempDir -InitialPort 3 -Interactive $true
Assert-Equal $true $result "烧录成功应返回 true"

# 清理
Remove-Item $tempDir -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item $isolated -Recurse -Force -ErrorAction SilentlyContinue

if ($failures.Count -gt 0) {
    Write-Host ""
    Write-Host ("FAIL build-flash-utils.test: " + $failures.Count + " failure(s)")
    exit 1
}

Write-Host ""
Write-Host "PASS build-flash-utils.test"
exit 0
