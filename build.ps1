$ErrorActionPreference = "Stop"

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

    $hasParallelOption = $false
    foreach ($argument in $args) {
        if (($argument -match "^-j[0-9]+$") -or
            ($argument -match "^--jobs(?:=|$)")) {
            $hasParallelOption = $true
            break
        }
    }

    if (-not $hasParallelOption) {
        $sconsArguments += "-j8"
    }

    $sconsArguments += $args

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
}
catch {
    [Console]::Error.WriteLine("ERROR: " + $_.Exception.Message)
    exit 1
}
