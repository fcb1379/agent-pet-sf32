$ErrorActionPreference = "Stop"

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$compiler = Get-Command clang -ErrorAction SilentlyContinue
if ($null -eq $compiler)
{
    $compiler = Get-Command gcc -ErrorAction SilentlyContinue
}

$outputPath = Join-Path $env:TEMP "pet_behavior_host_test.exe"
if ($null -ne $compiler)
{
    & $compiler.Source `
        "-std=c11" `
        "-Wall" `
        "-Wextra" `
        "-Werror" `
        "-I$repositoryRoot\work\watch_bt_audio_template\src\app_utils" `
        "-I$repositoryRoot\work\watch_bt_audio_template\src\gui_apps\pet" `
        "$repositoryRoot\work\watch_bt_audio_template\src\app_utils\pet_behavior.c" `
        "$repositoryRoot\work\watch_bt_audio_template\src\gui_apps\pet\pet_state_assets.c" `
        "$repositoryRoot\tests\pet_behavior_host_test.c" `
        "-o" `
        $outputPath

    & $outputPath
    exit $LASTEXITCODE
}

$vcVars = Get-ChildItem `
    -Path "$env:ProgramFiles\Microsoft Visual Studio\2022" `
    -Filter "vcvars64.bat" `
    -Recurse `
    -ErrorAction SilentlyContinue | Select-Object -First 1
if ($null -ne $vcVars)
{
    $buildPath = Join-Path $env:TEMP "pet_behavior_host_test"
    New-Item -Path $buildPath -ItemType Directory -Force | Out-Null
    $msvcCommand = 'call "{0}" && cl /nologo /std:c11 /W4 /WX ' +
        '/I"{1}\work\watch_bt_audio_template\src\app_utils" ' +
        '/I"{1}\work\watch_bt_audio_template\src\gui_apps\pet" ' +
        '"{1}\work\watch_bt_audio_template\src\app_utils\pet_behavior.c" ' +
        '"{1}\work\watch_bt_audio_template\src\gui_apps\pet\pet_state_assets.c" ' +
        '"{1}\tests\pet_behavior_host_test.c" /Fe:"{2}"'
    $msvcCommand = $msvcCommand -f $vcVars.FullName, $repositoryRoot, $outputPath
    Push-Location $buildPath
    try
    {
        & cmd.exe /d /c $msvcCommand
        if (0 -ne $LASTEXITCODE)
        {
            exit $LASTEXITCODE
        }
    }
    finally
    {
        Pop-Location
    }
    & $outputPath
    exit $LASTEXITCODE
}

$wsl = Get-Command wsl -ErrorAction SilentlyContinue
if ($null -ne $wsl)
{
    $wslRoot = (& $wsl.Source --cd $repositoryRoot pwd).Trim()
    & $wsl.Source sh -lc "cc -std=c11 -Wall -Wextra -Werror -I'$wslRoot/work/watch_bt_audio_template/src/app_utils' -I'$wslRoot/work/watch_bt_audio_template/src/gui_apps/pet' '$wslRoot/work/watch_bt_audio_template/src/app_utils/pet_behavior.c' '$wslRoot/work/watch_bt_audio_template/src/gui_apps/pet/pet_state_assets.c' '$wslRoot/tests/pet_behavior_host_test.c' -o /tmp/pet_behavior_host_test && /tmp/pet_behavior_host_test"
    exit $LASTEXITCODE
}

throw "No supported C compiler was found (clang, gcc, MSVC, or WSL cc)."
