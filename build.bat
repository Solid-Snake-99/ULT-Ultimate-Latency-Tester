@echo off
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Visual Studio not found. Install "Desktop development with C++" workload.
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo Visual Studio C++ tools not found. Install "Desktop development with C++" workload.
    exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
if exist app.rc (
    rc.exe /nologo app.rc
)
cl /nologo /O2 /Oi /Ot /GF /MP /EHsc /std:c++17 /utf-8 /DUNICODE /D_UNICODE main.cpp app.res /link /SUBSYSTEM:WINDOWS "/OUT:ULT Ultimate Latency Tester.exe"
echo.
echo Done: ULT Ultimate Latency Tester.exe
