@echo off
setlocal EnableDelayedExpansion
set NoDefaultCurrentDirectoryInExePath=
set "SIMULATOR_DIR=%~dp0"

REM Path to SiFli-SDK (repo submodule by default, override via env var)
if not defined SIFLI_SIM_SDK set "SIFLI_SIM_SDK=%~dp0..\..\..\sdk"
for %%I in ("%SIFLI_SIM_SDK%") do set "SIFLI_SIM_SDK=%%~fI"
set "SIM_PYTHON=python"

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
echo build finished, exitcode=!RC!
exit /b !RC!
