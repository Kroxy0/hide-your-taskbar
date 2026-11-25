@echo off
setlocal
cd /d "%~dp0"
set "root=%~dp0"
where cl >nul 2>nul
if errorlevel 1 (
    call "%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
    if errorlevel 1 exit /b 1
)
set "out=%~1"
if not defined out set "out=build"
if not exist "%out%" mkdir "%out%"
pushd "%out%"
if errorlevel 1 exit /b 1

set "flags=/nologo /std:c++20 /EHsc /W4 /O2 /MT"
if /i "%~2"=="--controller-only" goto controller

cl %flags% /LD /I "%root%src\shared" /FeTaskbarAppearance.dll ^
    "%root%src\taskbar\TaskbarBrush.cpp" ^
    "%root%src\taskbar\TaskbarDiagnostics.cpp" ^
    windowsapp.lib oleaut32.lib /link /EXPORT:DllGetClassObject,PRIVATE
if errorlevel 1 goto failed

:controller
rc /nologo /i "%root%resources" /fo SettingsDialog.res "%root%resources\SettingsDialog.rc"
if errorlevel 1 goto failed
cl %flags% /DUNICODE /I "%root%src\shared" /I "%root%resources" /FeHideYourTaskbar.exe ^
    "%root%src\app\Main.cpp" ^
    "%root%src\app\ControllerSession.cpp" ^
    "%root%src\app\Tray.cpp" ^
    "%root%src\app\AppearanceMonitor.cpp" ^
    "%root%src\app\Preferences.cpp" ^
    "%root%src\app\SettingsWindow.cpp" ^
    "%root%src\app\SettingsTheme.cpp" ^
    "%root%src\app\ColorPicker.cpp" ^
    "%root%src\app\ColorSpectrum.cpp" ^
    SettingsDialog.res windowsapp.lib user32.lib shell32.lib advapi32.lib comctl32.lib gdi32.lib msimg32.lib dwmapi.lib ^
    /link /SUBSYSTEM:WINDOWS /ENTRY:wmainCRTStartup
if errorlevel 1 goto failed
popd
exit /b 0

:failed
popd
exit /b 1
