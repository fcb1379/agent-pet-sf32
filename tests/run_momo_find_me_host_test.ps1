$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$output = Join-Path $env:TEMP 'momo_find_me_host_test.exe'
$compiler = Get-Command clang -ErrorAction SilentlyContinue
if ($null -eq $compiler) {
    $compiler = Get-Command gcc -ErrorAction SilentlyContinue
}
if ($null -eq $compiler) {
    $vsDevCmd = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat'
    if (-not (Test-Path -LiteralPath $vsDevCmd)) {
        throw 'No supported C compiler is available'
    }
    $command = 'call "' + $vsDevCmd + '" -arch=x64 -host_arch=x64 >nul' +
        ' && cl.exe /nologo /utf-8 /W4 /WX /std:c11' +
        ' /I "' + (Join-Path $PSScriptRoot 'momo_find_me_host/include') + '"' +
        ' /I "' + (Join-Path $repoRoot 'work/watch_bt_audio_template/src/app_utils') + '"' +
        ' "' + (Join-Path $PSScriptRoot 'momo_find_me_host_test.c') + '"' +
        ' "' + (Join-Path $repoRoot 'work/watch_bt_audio_template/src/app_utils/momo_find_me.c') + '"' +
        ' /Fe:"' + $output + '" && "' + $output + '"'
    & cmd.exe /d /c $command
    exit $LASTEXITCODE
}

& $compiler.Source -std=c11 -Wall -Wextra -Werror `
    -I (Join-Path $PSScriptRoot 'momo_find_me_host/include') `
    -I (Join-Path $repoRoot 'work/watch_bt_audio_template/src/app_utils') `
    (Join-Path $PSScriptRoot 'momo_find_me_host_test.c') `
    (Join-Path $repoRoot 'work/watch_bt_audio_template/src/app_utils/momo_find_me.c') `
    -o $output
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $output
exit $LASTEXITCODE
