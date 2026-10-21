@echo off
rem Builds RDR2Coop.asi, coopbot.exe and core_tests.exe with Visual Studio 2022
rem or 2026 ("Desktop development with C++"), then runs the tests.
setlocal
cd /d "%~dp0"
set "RC=1"

rem Find Visual Studio with the C++ tools. vswhere's folder goes on PATH rather
rem than being quoted inside FOR /F, where "Program Files (x86)" can break the
rem command line. -products * also finds the Build Tools edition.
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
set "VSPATH="
set "VSMAJOR="
set "VSWHERE_ARGS=-latest -products * -version 17.0 -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
for /f "usebackq tokens=*" %%i in (`vswhere %VSWHERE_ARGS% -property installationPath 2^>nul`) do set "VSPATH=%%i"
for /f "usebackq tokens=1 delims=." %%i in (`vswhere %VSWHERE_ARGS% -property installationVersion 2^>nul`) do set "VSMAJOR=%%i"
if not defined VSPATH goto :no_vs

rem Prefer the CMake that ships with that Visual Studio: it always knows its
rem generator, while an older CMake on PATH may not.
set "VSCMAKE=%VSPATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
if exist "%VSCMAKE%\cmake.exe" set "PATH=%VSCMAKE%;%PATH%"
where cmake >nul 2>nul
if errorlevel 1 goto :no_cmake

rem VS 2022 by name; newer versions are what CMake picks by default.
set "GENERATOR_ARG="
if "%VSMAJOR%"=="17" set GENERATOR_ARG=-G "Visual Studio 17 2022"
echo Using Visual Studio %VSMAJOR% at %VSPATH%

set "SDKDIR=sdk"
if exist "sdk\inc\main.h" if exist "sdk\lib\ScriptHookRDR2.lib" goto :configure
rem A common slip: the zip's own folder ends up between sdk\ and inc\. Use it there.
for /d %%d in (sdk\*) do if exist "%%~d\inc\main.h" if exist "%%~d\lib\ScriptHookRDR2.lib" set "SDKDIR=%%~d"
if not "%SDKDIR%"=="sdk" echo Using the Script Hook RDR2 SDK in "%SDKDIR%"
if not "%SDKDIR%"=="sdk" goto :configure
echo.
echo NOTE: Script Hook RDR2 SDK not found, so only the test tools will be built.
echo       Extract the SDK so that sdk\inc\main.h and sdk\lib\ScriptHookRDR2.lib exist.
echo.

:configure
rem Absolute, with forward slashes: CMake keeps a relative -D path as given.
set "SDKPATH=%CD%\%SDKDIR%"
set "SDKPATH=%SDKPATH:\=/%"
cmake -S . -B build %GENERATOR_ARG% -A x64 "-DRDR2COOP_SDK_DIR:PATH=%SDKPATH%"
if errorlevel 1 goto :fail_configure
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
set "RC=0"
goto :finish

:no_vs
echo Visual Studio 2022 or newer with "Desktop development with C++" was not found.
echo Install it from https://visualstudio.microsoft.com/ and run this again.
goto :finish

:no_cmake
echo CMake was not found. In the Visual Studio Installer, add "C++ CMake tools for
echo Windows" (part of "Desktop development with C++") and run this again.
goto :finish

:fail_configure
echo.
echo CMake configure failed - see the messages above. If you switched Visual Studio
echo versions since the last build, delete the "build" folder and run this again.
goto :finish

:fail
echo.
echo Build or tests failed - see the messages above.
goto :finish

:finish
rem Started by double-click? Keep the window open so the result can be read.
rem (timeout returns at once when input is redirected, so scripts don't hang.)
setlocal EnableDelayedExpansion
if /i "!cmdcmdline:%~nx0=!"=="!cmdcmdline!" exit /b %RC%
echo.
echo Press any key to close this window.
timeout /t -1 >nul 2>nul
exit /b %RC%
