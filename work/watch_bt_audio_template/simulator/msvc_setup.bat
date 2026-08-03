@echo off
setlocal EnableDelayedExpansion

REM Override these variables when Visual Studio or the Windows SDK moves.
if not defined SIFLI_SIM_MSVC_ROOT set "SIFLI_SIM_MSVC_ROOT=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207"
if not defined SIFLI_SIM_SDK_INCLUDE set "SIFLI_SIM_SDK_INCLUDE=C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0"
if not defined SIFLI_SIM_SDK_LIB set "SIFLI_SIM_SDK_LIB=C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0"

if not exist "!SIFLI_SIM_MSVC_ROOT!\bin\Hostx64\x86\cl.exe" (
    echo ERROR: MSVC x86 compiler not found under !SIFLI_SIM_MSVC_ROOT!
    exit /b 3
)
if not exist "!SIFLI_SIM_SDK_INCLUDE!\ucrt" (
    echo ERROR: Windows SDK headers not found under !SIFLI_SIM_SDK_INCLUDE!
    exit /b 4
)
if not exist "!SIFLI_SIM_SDK_LIB!\ucrt\x86" (
    echo ERROR: Windows SDK libraries not found under !SIFLI_SIM_SDK_LIB!
    exit /b 5
)

subst x: /d >nul 2>&1
subst y: /d >nul 2>&1
subst l: /d >nul 2>&1
subst x: "!SIFLI_SIM_MSVC_ROOT!"
subst y: "!SIFLI_SIM_SDK_INCLUDE!"
subst l: "!SIFLI_SIM_SDK_LIB!"

exit /b 0
