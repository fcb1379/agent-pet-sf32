@echo off
setlocal EnableDelayedExpansion
set NoDefaultCurrentDirectoryInExePath=
set "SIMULATOR_DIR=%~dp0"

REM Path to SiFli-SDK (repo submodule by default, override via env var)
set "DEFAULT_SIM_SDK=%~dp0..\..\..\sdk"
for %%I in ("%DEFAULT_SIM_SDK%") do set "DEFAULT_SIM_SDK=%%~fI"
if not defined SIFLI_SIM_SDK set "SIFLI_SIM_SDK=%DEFAULT_SIM_SDK%"
for %%I in ("%SIFLI_SIM_SDK%") do set "SIFLI_SIM_SDK=%%~fI"
set "SIM_PYTHON=python"

REM A normal clone does not populate submodules. Bootstrap the pinned SDK on
REM first run so preview_simulator.bat works directly from the main repo.
if not exist "%SIFLI_SIM_SDK%\tools\build\building.py" (
    if /I not "%SIFLI_SIM_SDK%"=="%DEFAULT_SIM_SDK%" (
        echo ERROR: SIFLI_SIM_SDK is not a valid SiFli-SDK checkout:
        echo %SIFLI_SIM_SDK%
        exit /b 2
    )
    where git >nul 2>nul
    if errorlevel 1 (
        echo ERROR: Git is required to initialize the SDK submodule.
        exit /b 2
    )
    for %%I in ("%SIMULATOR_DIR%..\..\..") do set "REPO_ROOT=%%~fI"
    echo Initializing the pinned SiFli-SDK submodule...
    git -C "%REPO_ROOT%" submodule update --init --recursive sdk
    if errorlevel 1 exit /b !errorlevel!
)

REM Use SiFli-ENV when configured. The PC simulator can also use system
REM Python + SCons because it is compiled with MSVC rather than the MCU toolchain.
if defined SIFLI_ENV (
    if not exist "%SIFLI_ENV%\tools\ConEmu\ConEmu\CmdInit.cmd" (
        echo ERROR: SIFLI_ENV does not point to a valid SiFli-ENV installation:
        echo %SIFLI_ENV%
        exit /b 2
    )

    call "%SIFLI_ENV%\tools\ConEmu\ConEmu\CmdInit.cmd"
    if errorlevel 1 exit /b !errorlevel!

    cd /d "%SIFLI_SIM_SDK%"
    call set_env.bat
    if errorlevel 1 exit /b !errorlevel!

    set "SIM_PYTHON=%SIFLI_ENV%\tools\python-3.11.9-amd64\python.exe"
) else (
    set "SIFLI_SDK=%SIFLI_SIM_SDK%"
    echo SIFLI_ENV is not configured; using system Python and MSVC.
)

REM The SDK v2.4 PC board script points at VS2017. Establish current MSVC and
REM Windows SDK drive mappings first; the failed legacy substitutions then
REM leave these valid mappings intact.
call "%SIMULATOR_DIR%msvc_setup.bat"
if errorlevel 1 exit /b !errorlevel!

set "SIFLI_SDK=%SIFLI_SIM_SDK%"
set "PYTHONPATH=%SIFLI_SDK%\tools\build;%PYTHONPATH%"

REM Build the real watch application graph with the pc_hcpu overlay. This
REM includes the GUI framework, Main launcher, every app, and all resources.
cd /d "%SIMULATOR_DIR%..\project"
"%SIM_PYTHON%" -m SCons --board=pc -j8
set RC=!errorlevel!

REM SDK v2.4 emits this one PC object beside its source because the source is
REM referenced by an absolute path. It is reproducible and must not dirty the submodule.
if /I "%SIFLI_SIM_SDK%"=="%DEFAULT_SIM_SDK%" (
    if exist "%SIFLI_SIM_SDK%\drivers\hal\bf0_hal_hlp.obj" (
        del /q "%SIFLI_SIM_SDK%\drivers\hal\bf0_hal_hlp.obj"
    )
)

echo build finished, exitcode=!RC!
exit /b !RC!
