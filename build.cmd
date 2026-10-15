@echo off
setlocal
cd /d "%~dp0"
if defined VSCMD_VER if /i "%VSCMD_ARG_TGT_ARCH%"=="x64" goto build
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo Install Visual Studio 2022 Build Tools with Desktop development with C++ and the Windows SDK.
  exit /b 1
)
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_ROOT=%%I"
if not defined VS_ROOT (
  echo A Visual Studio C++ toolchain was not found.
  exit /b 1
)
call "%VS_ROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
:build
if not exist build mkdir build
pushd src
rc /nologo /fo ..\build\resources.res settings-dialog.rc
if errorlevel 1 exit /b 1
popd
cl /nologo /std:c++17 /EHsc /W4 /O2 /MT /LD /DUNICODE /D_UNICODE /Fo:build\obs-input-fx.obj src\obs-input-fx.cpp build\resources.res /link /OUT:build\obs-input-fx.dll /IMPLIB:build\obs-input-fx.lib user32.lib comctl32.lib comdlg32.lib ws2_32.lib advapi32.lib crypt32.lib bcrypt.lib dwmapi.lib gdi32.lib
if errorlevel 1 exit /b 1
if /i not "%~1"=="--tests" exit /b 0
cl /nologo /std:c++17 /EHsc /W4 /O2 /MT /DUNICODE /D_UNICODE /Fo:build\windows-app.obj tests\fixtures\windows-app.cpp /link /SUBSYSTEM:WINDOWS /OUT:build\input-fx-fixture.exe user32.lib gdi32.lib
exit /b %errorlevel%
