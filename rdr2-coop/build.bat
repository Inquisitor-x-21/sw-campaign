@echo off
rem Builds RDR2Coop.asi, coopbot.exe and core_tests.exe with Visual Studio 2022,
rem then runs the tests. Needs "Desktop development with C++" installed.
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul
if not errorlevel 1 goto :have_cmake

rem No cmake on PATH: use the one that ships with Visual Studio.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :no_vs
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH goto :no_vs
set "PATH=%VSPATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"
where cmake >nul 2>nul
if errorlevel 1 goto :no_vs

:have_cmake
if exist "sdk\inc\main.h" goto :configure
echo.
echo NOTE: Script Hook RDR2 SDK not found, so only the test tools will be built.
echo       Extract the SDK so that sdk\inc\main.h and sdk\lib\ScriptHookRDR2.lib exist.
echo.

:configure
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 goto :fail
cmake --build build --config Release
if errorlevel 1 goto :fail

echo.
echo Running tests...
build\Release\core_tests.exe
if errorlevel 1 goto :fail
build\Release\coopbot.exe selftest
if errorlevel 1 goto :fail

echo.
echo Done:
if exist build\Release\RDR2Coop.asi echo   build\Release\RDR2Coop.asi + RDR2Coop.ini  - copy both to the game folder, or run install.bat
echo   build\Release\coopbot.exe  - test bots
exit /b 0

:no_vs
echo Visual Studio 2022 with "Desktop development with C++" was not found.
echo Install it from https://visualstudio.microsoft.com/ and run this again.
exit /b 1

:fail
echo.
echo Build or tests failed - see the messages above.
exit /b 1
