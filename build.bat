@echo off
rem Builds the 32-bit proxy DLL: build\XINPUT1_3.dll from the folders under src\.
rem Needs the Visual Studio Build Tools (or Visual Studio) with the C++ x86/x64 tools.
rem Run it from a normal prompt; it finds the tools itself. From an x86 Developer Command
rem Prompt it uses that environment as it is.
setlocal EnableExtensions

if not "%~1"=="" (
    echo usage: build.bat
    exit /b 1
)

rem Already a 32-bit MSVC environment?
where cl >nul 2>nul
if errorlevel 1 goto :find
if /i "%VSCMD_ARG_TGT_ARCH%"=="x86" goto :build

:find
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :notools
set "VSPATH="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH goto :notools
set "VCVARS=%VSPATH%\VC\Auxiliary\Build\vcvars32.bat"
if not exist "%VCVARS%" goto :notools
call "%VCVARS%" >nul
if errorlevel 1 exit /b 1
goto :build

:notools
echo error: no Visual Studio build tools found.
echo Install the "Build Tools for Visual Studio" with the workload "Desktop development with C++"
echo ^(component MSVC x86/x64 build tools^), or run this from an x86 Developer Command Prompt.
exit /b 1

:build
cd /d "%~dp0"
if not exist build mkdir build
if not exist build\obj mkdir build\obj
cl /nologo /O2 /MT /W4 /EHsc /LD /I src src\core\*.cpp src\effects\*.cpp src\hud\*.cpp src\input\*.cpp src\menu\*.cpp src\shadow\*.cpp src\view\*.cpp /Fobuild\obj\ /Fdbuild\obj\ /link /DEF:src\core\xinput.def /OUT:build\XINPUT1_3.dll /IMPLIB:build\obj\XINPUT1_3.lib || exit /b 1
echo built build\XINPUT1_3.dll
exit /b 0
