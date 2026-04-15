@echo off
setlocal

set "ROOT=%~dp0"
set "VS_VCVARS=D:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"

if not exist "%VS_VCVARS%" (
    echo vcvarsall.bat not found: "%VS_VCVARS%"
    exit /b 1
)

call "%VS_VCVARS%" x86
if errorlevel 1 exit /b 1

if not exist "%ROOT%build" mkdir "%ROOT%build"
if not exist "%ROOT%build\obj" mkdir "%ROOT%build\obj"
if not exist "%ROOT%build\bin" mkdir "%ROOT%build\bin"

cl ^
    /nologo ^
    /std:c++20 ^
    /O2 ^
    /MT ^
    /EHsc ^
    /W4 ^
    /DUNICODE ^
    /D_UNICODE ^
    /DWIN32_LEAN_AND_MEAN ^
    /D_CRT_SECURE_NO_WARNINGS ^
    /I "%ROOT%src" ^
    /c ^
    "%ROOT%src\common.cpp" ^
    "%ROOT%src\borderless_hooks.cpp" ^
    "%ROOT%src\dinput8_proxy.cpp" ^
    /Fo"%ROOT%build\obj\\"
if errorlevel 1 exit /b 1

link ^
    /nologo ^
    /DLL ^
    /MACHINE:X86 ^
    /OUT:"%ROOT%build\bin\dinput8.dll" ^
    /DEF:"%ROOT%dinput8_proxy.def" ^
    "%ROOT%build\obj\common.obj" ^
    "%ROOT%build\obj\borderless_hooks.obj" ^
    "%ROOT%build\obj\dinput8_proxy.obj" ^
    user32.lib ^
    gdi32.lib ^
    kernel32.lib ^
    ole32.lib
if errorlevel 1 exit /b 1

if exist "%ROOT%shogun2_mod.ini" copy /Y "%ROOT%shogun2_mod.ini" "%ROOT%build\bin\shogun2_mod.ini" >nul

echo Built "%ROOT%build\bin\dinput8.dll"
