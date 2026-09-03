$ErrorActionPreference = "Stop"

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$sourceRoot = Join-Path $repositoryRoot "work\watch_bt_audio_template\src"
$petRoot = Join-Path $sourceRoot "gui_apps\pet"
$appUtilsRoot = Join-Path $sourceRoot "app_utils"
$outputPath = Join-Path $env:TEMP "momo_agent_squad_host_test.exe"

$compiler = Get-Command gcc -ErrorAction SilentlyContinue
if ($null -ne $compiler)
{
    & $compiler.Source -std=c11 -Wall -Wextra -Werror `
        "-I$petRoot" "-I$appUtilsRoot" `
        (Join-Path $petRoot "momo_agent_squad.c") `
        (Join-Path $PSScriptRoot "momo_agent_squad_host_test.c") `
        -o $outputPath
    & $outputPath
    exit $LASTEXITCODE
}

$wslRoot = (& wsl.exe -e wslpath -a $repositoryRoot).Trim()
& wsl sh -lc "cc -std=c11 -Wall -Wextra -Werror -I'$wslRoot/work/watch_bt_audio_template/src/gui_apps/pet' -I'$wslRoot/work/watch_bt_audio_template/src/app_utils' '$wslRoot/work/watch_bt_audio_template/src/gui_apps/pet/momo_agent_squad.c' '$wslRoot/tests/momo_agent_squad_host_test.c' -o /tmp/momo_agent_squad_host_test && /tmp/momo_agent_squad_host_test"
exit $LASTEXITCODE
