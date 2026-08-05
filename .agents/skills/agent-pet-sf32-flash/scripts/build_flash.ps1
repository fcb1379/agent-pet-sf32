[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = "High")]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern("^COM[0-9]+$")]
    [string]$Port,

    [switch]$BuildOnly,

    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..\..")).Path
$sdkExport = Join-Path $repoRoot "sdk\export.ps1"
$projectDir = Join-Path $repoRoot "work\watch_bt_audio_template\project"
$buildDir = Join-Path $projectDir "build_sf32lb52-lchspi-ulp_hcpu"
$diskDir = Join-Path $repoRoot "work\watch_bt_audio_template\disk"
$sourceDir = Join-Path $repoRoot "work\watch_bt_audio_template\src"
$images = @(
    [pscustomobject]@{ Name = "bootloader"; RelativePath = "bootloader\bootloader.bin"; Address = "0x12010000" },
    [pscustomobject]@{ Name = "main"; RelativePath = "main.bin"; Address = "0x12020000" },
    [pscustomobject]@{ Name = "ftab"; RelativePath = "ftab\ftab.bin"; Address = "0x12000000" },
    [pscustomobject]@{ Name = "fs_root"; RelativePath = "fs_root.bin"; Address = "0x129A0000" }
)

function Get-NewestWriteTime
{
    param([System.IO.FileInfo[]]$Files)

    if (0 -eq $Files.Count)
    {
        return [datetime]::MinValue
    }

    return ($Files | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1).LastWriteTimeUtc
}

foreach ($requiredPath in @($sdkExport, $projectDir, $diskDir, $sourceDir))
{
    if (-not (Test-Path -LiteralPath $requiredPath))
    {
        throw "Required project path is missing: $requiredPath"
    }
}

if (-not $SkipBuild)
{
    . $sdkExport
    if (-not $?)
    {
        throw "Failed to initialize the SiFli SDK environment: $sdkExport"
    }

    if (-not (Get-Command scons -ErrorAction SilentlyContinue))
    {
        throw "scons is unavailable after loading sdk/export.ps1"
    }

    Push-Location $projectDir
    try
    {
        & scons --board=sf32lb52-lchspi-ulp --board_search_path=../boards -j8
        if (0 -ne $LASTEXITCODE)
        {
            throw "SCons failed with exit code $LASTEXITCODE"
        }
    }
    finally
    {
        Pop-Location
    }
}

foreach ($image in $images)
{
    $imagePath = Join-Path $buildDir $image.RelativePath
    if (-not (Test-Path -LiteralPath $imagePath -PathType Leaf))
    {
        throw "Required image is missing: $imagePath"
    }

    if (0 -eq (Get-Item -LiteralPath $imagePath).Length)
    {
        throw "Required image is empty: $imagePath"
    }
}

$mainImage = Get-Item -LiteralPath (Join-Path $buildDir "main.bin")
$mainInputs = @(
    Get-ChildItem -LiteralPath $sourceDir -Recurse -File
    Get-ChildItem -LiteralPath $projectDir -File | Where-Object {
        $_.Name -in @("SConstruct", "SConscript", "proj.conf", "Kconfig", "Kconfig.proj", "rtconfig_project.h")
    }
)
$newestMainInput = Get-NewestWriteTime -Files $mainInputs
if ($mainImage.LastWriteTimeUtc -lt $newestMainInput)
{
    throw "main.bin is older than an application or project input; rebuild without -SkipBuild"
}

$filesystemImage = Get-Item -LiteralPath (Join-Path $buildDir "fs_root.bin")
$diskInputs = @(Get-ChildItem -LiteralPath $diskDir -Recurse -File)
$newestDiskInput = Get-NewestWriteTime -Files $diskInputs
if ($filesystemImage.LastWriteTimeUtc -lt $newestDiskInput)
{
    throw "fs_root.bin is older than a disk asset; rebuild without -SkipBuild"
}

foreach ($image in $images)
{
    $imagePath = Join-Path $buildDir $image.RelativePath
    $file = Get-Item -LiteralPath $imagePath
    $hash = (Get-FileHash -LiteralPath $imagePath -Algorithm SHA256).Hash
    Write-Output ("{0}: {1} bytes, {2}, SHA256={3}" -f $image.Name, $file.Length, $image.Address, $hash)
}

if ($BuildOnly)
{
    Write-Output "Build and image validation completed; flashing was not requested."
    return
}

$availablePorts = [System.IO.Ports.SerialPort]::GetPortNames()
if ($Port -notin $availablePorts)
{
    throw "Requested port $Port is not enumerated. Available ports: $($availablePorts -join ', ')"
}

$sftoolCommand = Get-Command sftool -ErrorAction SilentlyContinue
if ($null -ne $sftoolCommand)
{
    $sftoolPath = $sftoolCommand.Source
}
else
{
    $sftoolRoot = Join-Path $env:USERPROFILE ".sifli\tools\sftool"
    $sftoolFile = Get-ChildItem -LiteralPath $sftoolRoot -Recurse -Filter "sftool.exe" -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1
    if ($null -eq $sftoolFile)
    {
        throw "sftool.exe was not found in PATH or $sftoolRoot"
    }

    $sftoolPath = $sftoolFile.FullName
}

$flashArguments = @($images | ForEach-Object { "$($_.RelativePath)@$($_.Address)" })
if ($PSCmdlet.ShouldProcess($Port, "write bootloader, main, ftab, and fs_root images"))
{
    Push-Location $buildDir
    try
    {
        & $sftoolPath -p $Port -c SF32LB52 write_flash @flashArguments
        if (0 -ne $LASTEXITCODE)
        {
            throw "sftool failed with exit code $LASTEXITCODE"
        }
    }
    finally
    {
        Pop-Location
    }

    Write-Output "Flash completed successfully on $Port."
}
