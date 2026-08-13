$ErrorActionPreference = "Stop"

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$petRoot = Join-Path $repositoryRoot "work\watch_bt_audio_template\src\gui_apps\pet"
$outputPath = Join-Path $env:TEMP "momo_stroking_host_test.exe"

if (Get-Command clang -ErrorAction SilentlyContinue) {
    & clang -std=c11 -Wall -Wextra -Werror `
        "-I$petRoot" `
        (Join-Path $petRoot "momo_stroking.c") `
        (Join-Path $PSScriptRoot "momo_stroking_host_test.c") `
        -o $outputPath
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $outputPath
    exit $LASTEXITCODE
}

if (Get-Command gcc -ErrorAction SilentlyContinue) {
    & gcc -std=c11 -Wall -Wextra -Werror `
        "-I$petRoot" `
        (Join-Path $petRoot "momo_stroking.c") `
        (Join-Path $PSScriptRoot "momo_stroking_host_test.c") `
        -o $outputPath
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $outputPath
    exit $LASTEXITCODE
}

$driveLetter = $repositoryRoot.Substring(0, 1).ToLowerInvariant()
$relativeRoot = $repositoryRoot.Substring(2).Replace('\', '/')
$wslRoot = "/mnt/$driveLetter$relativeRoot"
& wsl sh -lc "cc -std=c11 -Wall -Wextra -Werror -I'$wslRoot/work/watch_bt_audio_template/src/gui_apps/pet' '$wslRoot/work/watch_bt_audio_template/src/gui_apps/pet/momo_stroking.c' '$wslRoot/tests/momo_stroking_host_test.c' -o /tmp/momo_stroking_host_test && /tmp/momo_stroking_host_test"
exit $LASTEXITCODE
