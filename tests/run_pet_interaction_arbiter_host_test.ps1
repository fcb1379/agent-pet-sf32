$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$source = Join-Path $repoRoot "work\watch_bt_audio_template\src\gui_apps\pet\pet_interaction_arbiter.c"
$test = Join-Path $PSScriptRoot "pet_interaction_arbiter_host_test.c"
$include = Join-Path $repoRoot "work\watch_bt_audio_template\src\gui_apps\pet"
$output = Join-Path $env:TEMP "pet_interaction_arbiter_host_test.exe"
$compiler = Get-Command clang -ErrorAction SilentlyContinue
if ($null -eq $compiler) { $compiler = Get-Command gcc -ErrorAction SilentlyContinue }
if ($null -ne $compiler)
{
    & $compiler.Source -std=c11 -Wall -Wextra -Werror -pedantic -I $include $source $test -o $output
    & $output
    exit $LASTEXITCODE
}
$repoWsl = (wsl.exe wslpath -a ($repoRoot -replace '\\', '/')).Trim()
wsl.exe sh -lc "cc -std=c11 -Wall -Wextra -Werror -pedantic -I '$repoWsl/work/watch_bt_audio_template/src/gui_apps/pet' '$repoWsl/work/watch_bt_audio_template/src/gui_apps/pet/pet_interaction_arbiter.c' '$repoWsl/tests/pet_interaction_arbiter_host_test.c' -o /tmp/pet_interaction_arbiter_host_test && /tmp/pet_interaction_arbiter_host_test"
exit $LASTEXITCODE
