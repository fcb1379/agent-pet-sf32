@echo off
setlocal

set "APP_DIR=%~dp0"

echo [1/2] Building the simulator from the latest hardware UI sources...
call "%APP_DIR%simulator\build.bat"
if errorlevel 1 (
    echo.
    echo Simulator build failed. The hardware UI was not started.
    pause
    exit /b 1
)

set "SIMULATOR_EXE=%APP_DIR%simulator\build\bf0_ap.exe"
if not exist "%SIMULATOR_EXE%" (
    echo.
    echo Build succeeded, but the simulator executable was not found:
    echo %SIMULATOR_EXE%
    pause
    exit /b 2
)

echo [2/2] Starting the simulator...
start "Agent Pet LVGL Simulator" "%SIMULATOR_EXE%"
exit /b 0
