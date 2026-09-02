$ErrorActionPreference = "Stop"

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$petRoot = Join-Path $repositoryRoot "work\watch_bt_audio_template\src\gui_apps\pet"
$outputPath = Join-Path $env:TEMP "momo_memory_calendar_host_test.exe"

$compiler = Get-Command gcc -ErrorAction SilentlyContinue
if ($null -ne $compiler)
{
    & $compiler.Source -std=c11 -Wall -Wextra -Werror `
        "-I$petRoot" `
        (Join-Path $petRoot "momo_memory_calendar.c") `
        (Join-Path $PSScriptRoot "momo_memory_calendar_host_test.c") `
        -o $outputPath
    & $outputPath
    exit $LASTEXITCODE
}

$wslRoot = (& wsl.exe -e wslpath -a $repositoryRoot).Trim()
& wsl sh -lc "cc -std=c11 -Wall -Wextra -Werror -I'$wslRoot/work/watch_bt_audio_template/src/gui_apps/pet' '$wslRoot/work/watch_bt_audio_template/src/gui_apps/pet/momo_memory_calendar.c' '$wslRoot/tests/momo_memory_calendar_host_test.c' -o /tmp/momo_memory_calendar_host_test && /tmp/momo_memory_calendar_host_test"
exit $LASTEXITCODE
