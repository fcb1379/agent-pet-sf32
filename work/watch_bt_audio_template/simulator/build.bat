@echo off
setlocal EnableDelayedExpansion
set NoDefaultCurrentDirectoryInExePath=

REM Path to SiFli-SDK (repo submodule by default, override via env var)
if not defined SIFLI_SIM_SDK  set SIFLI_SIM_SDK=%~dp0..\..\..\..\sdk
REM Path to SiFli-ENV tools
if not defined SIFLI_ENV      set SIFLI_ENV=D:\Desktop\AI_Pet\env_latest

call "%SIFLI_ENV%\tools\ConEmu\ConEmu\CmdInit.cmd"

cd /d "%SIFLI_SIM_SDK%"
call set_env.bat

cd /d "%~dp0"
"%SIFLI_ENV%\tools\python-3.11.9-amd64\python.exe" -m SCons -j8
set RC=!errorlevel!
echo build finished, exitcode=!RC!
exit /b !RC!
