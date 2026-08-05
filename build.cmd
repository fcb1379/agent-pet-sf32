@echo off
setlocal

set "PROJECT_ROOT=%~dp0"

rem The legacy SiFli ENV exposes ENV_ROOT and ORG_PATH. Keep the build in this
rem process so the variables configured by set_env.bat remain available.
if defined ENV_ROOT if defined ORG_PATH goto legacy_build

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_ROOT%build.ps1" %*
exit /b %ERRORLEVEL%

:legacy_build
call "%PROJECT_ROOT%sdk\set_env.bat" gcc
if errorlevel 1 exit /b %ERRORLEVEL%

pushd "%PROJECT_ROOT%work\watch_bt_audio_template\project"
scons --board=sf32lb52-lchspi-ulp --board_search_path=../boards -j8 %*
set "BUILD_EXIT_CODE=%ERRORLEVEL%"
popd

exit /b %BUILD_EXIT_CODE%
