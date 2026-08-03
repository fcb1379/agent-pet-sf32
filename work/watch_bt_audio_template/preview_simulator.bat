@echo off
setlocal

set "APP_DIR=%~dp0"

echo [1/2] Building the complete watch UI simulator...
call "%APP_DIR%simulator\build.bat"
if errorlevel 1 (
    echo.
    echo Simulator build failed. The hardware UI was not started.
    pause
    exit /b 1
)

set "SIMULATOR_EXE=%APP_DIR%project\build_pc_hcpu\main.exe"
if not exist "%SIMULATOR_EXE%" (
    echo.
    echo Build succeeded, but the simulator executable was not found:
    echo %SIMULATOR_EXE%
    pause
    exit /b 2
)

echo [2/2] Starting the simulator...
pushd "%APP_DIR%project\build_pc_hcpu"
start "Agent Pet Complete UI Simulator" "%SIMULATOR_EXE%"
popd
exit /b 0
