$ErrorActionPreference = "Stop"

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$compiler = Get-Command clang -ErrorAction SilentlyContinue
if ($null -eq $compiler)
{
    $compiler = Get-Command gcc -ErrorAction SilentlyContinue
}

$outputPath = Join-Path $env:TEMP "agent_pet_protocol_host_test.exe"
if ($null -ne $compiler)
{
    & $compiler.Source `
        "-std=c11" `
        "-Wall" `
        "-Wextra" `
        "-Werror" `
        "-I$repositoryRoot\work\watch_bt_audio_template\src\app_utils" `
        "$repositoryRoot\work\watch_bt_audio_template\src\app_utils\agent_pet_protocol.c" `
        "$repositoryRoot\tests\agent_pet_protocol_host_test.c" `
        "-o" `
        $outputPath

    & $outputPath
    exit $LASTEXITCODE
}

$wslRoot = (& wsl --cd $repositoryRoot pwd).Trim()
& wsl sh -lc "cc -std=c11 -Wall -Wextra -Werror -I'$wslRoot/work/watch_bt_audio_template/src/app_utils' '$wslRoot/work/watch_bt_audio_template/src/app_utils/agent_pet_protocol.c' '$wslRoot/tests/agent_pet_protocol_host_test.c' -o /tmp/agent_pet_protocol_host_test && /tmp/agent_pet_protocol_host_test"
exit $LASTEXITCODE
