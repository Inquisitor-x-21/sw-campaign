@echo off
rem Copies the built plugin into the game folder.
rem   install.bat "C:\Program Files (x86)\Steam\steamapps\common\Red Dead Redemption 2"
rem or drag the game folder onto install.bat.
rem An existing RDR2Coop.ini is kept so your settings survive updates.
setlocal
cd /d "%~dp0"
set "RC=1"
set "GAME=%~1"
if "%GAME%"=="" (
    echo Usage: install.bat "path\to\Red Dead Redemption 2"
    echo        or drag the game folder onto install.bat
    goto :finish
)
if not exist "%GAME%\RDR2.exe" (
    echo RDR2.exe not found in "%GAME%".
    goto :finish
)
if not exist "build\Release\RDR2Coop.asi" (
    echo build\Release\RDR2Coop.asi not found - run build.bat first.
    goto :finish
)
if not exist "%GAME%\ScriptHookRDR2.dll" (
    echo WARNING: ScriptHookRDR2.dll is not in the game folder. Install Script Hook RDR2
    echo          ^(ScriptHookRDR2.dll and dinput8.dll^) or the plugin will not load.
)
if exist "%GAME%\ScriptHookRDR2.dll" if not exist "%GAME%\dinput8.dll" echo WARNING: dinput8.dll is not in the game folder. It is Script Hook's .asi loader; without it, or another .asi loader, RDR2Coop.asi will not load.
copy /y "build\Release\RDR2Coop.asi" "%GAME%\" >nul || goto :fail
if exist "%GAME%\RDR2Coop.ini" (
    echo Kept your existing RDR2Coop.ini.
) else (
    copy /y "config\RDR2Coop.ini" "%GAME%\" >nul || goto :fail
)
echo Installed RDR2Coop.asi to "%GAME%".
set "RC=0"
goto :finish

:fail
echo Copy failed. Is the game running? A game folder under Program Files also
echo needs install.bat to be run as administrator.
goto :finish

:finish
rem Keep the window open after a double-click or drag and drop. Claude Code
rem (which sets CLAUDECODE), CI, or a second argument "nopause" skip the wait.
if /i "%~2"=="nopause" exit /b %RC%
if defined CLAUDECODE exit /b %RC%
if defined CI exit /b %RC%
echo.
pause
exit /b %RC%
