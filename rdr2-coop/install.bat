@echo off
rem Copies the built plugin into the game folder.
rem   install.bat "C:\Program Files (x86)\Steam\steamapps\common\Red Dead Redemption 2"
rem An existing RDR2Coop.ini is kept so your settings survive updates.
setlocal
cd /d "%~dp0"
set "GAME=%~1"
if "%GAME%"=="" (
    echo Usage: install.bat "path\to\Red Dead Redemption 2"
    exit /b 1
)
if not exist "%GAME%\RDR2.exe" (
    echo RDR2.exe not found in "%GAME%".
    exit /b 1
)
if not exist "build\Release\RDR2Coop.asi" (
    echo build\Release\RDR2Coop.asi not found - run build.bat first.
    exit /b 1
)
if not exist "%GAME%\ScriptHookRDR2.dll" (
    echo WARNING: ScriptHookRDR2.dll is not in the game folder. Install Script Hook RDR2
    echo          ^(ScriptHookRDR2.dll and dinput8.dll^) or the plugin will not load.
)
copy /y "build\Release\RDR2Coop.asi" "%GAME%\" >nul || goto :fail
if exist "%GAME%\RDR2Coop.ini" (
    echo Kept your existing RDR2Coop.ini.
) else (
    copy /y "config\RDR2Coop.ini" "%GAME%\" >nul || goto :fail
)
echo Installed RDR2Coop.asi to "%GAME%".
exit /b 0

:fail
echo Copy failed. Is the game running?
exit /b 1
