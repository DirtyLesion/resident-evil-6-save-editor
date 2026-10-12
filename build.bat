@echo off
rem Builds build\RE6SaveEditor.exe with MSVC.
rem   build.bat                        build only
rem   build.bat test <savedata.bin>    also build and run the self-test against a save file
setlocal
cd /d "%~dp0"

where cl >nul 2>nul
if not errorlevel 1 goto build

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo Could not find Visual Studio with the C++ tools installed.
    exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul

:build
if not exist build mkdir build
rem /MT links the C++ runtime statically so the exe runs without the VC++ redistributable.
set CFLAGS=/nologo /std:c++20 /EHsc /O2 /MT /W4 /permissive- /utf-8 /DUNICODE /D_UNICODE /Fobuild\

cl %CFLAGS% src\main.cpp src\save.cpp src\fields.cpp /Fe:build\RE6SaveEditor.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED user32.lib gdi32.lib comctl32.lib comdlg32.lib shell32.lib advapi32.lib
if errorlevel 1 exit /b 1

if /i not "%~1"=="test" goto :eof
if "%~2"=="" (
    echo Usage: build.bat test path\to\savedata.bin
    exit /b 1
)
cl %CFLAGS% tests\selftest.cpp src\save.cpp src\fields.cpp /Fe:build\selftest.exe /link advapi32.lib
if errorlevel 1 exit /b 1
build\selftest.exe "%~2"
