@echo off
rem Builds RDR2Coop.asi, coopbot.exe and core_tests.exe with Visual Studio 2022
rem or 2026 ("Desktop development with C++"), then runs the tests.
rem   build.bat            waits for a key at the end (for double-clicking)
rem   build.bat nopause    doesn't wait (scripts)
setlocal
cd /d "%~dp0"
rem Every variable here starts with COOP_: cmake and MSBuild inherit them, and
rem short names collide (RC, for one, is the resource compiler to CMake).
set "COOP_RESULT=1"

rem Find Visual Studio with the C++ tools. vswhere's folder goes on PATH rather
rem than being quoted inside FOR /F, where "Program Files (x86)" can break the
rem command line. -products * also finds the Build Tools edition.
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
set "COOP_VSPATH="
set "COOP_VSMAJOR="
set "COOP_VSWHERE_ARGS=-latest -products * -version 17.0 -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
for /f "usebackq tokens=*" %%i in (`vswhere %COOP_VSWHERE_ARGS% -property installationPath 2^>nul`) do set "COOP_VSPATH=%%i"
for /f "usebackq tokens=1 delims=." %%i in (`vswhere %COOP_VSWHERE_ARGS% -property installationVersion 2^>nul`) do set "COOP_VSMAJOR=%%i"
if not defined COOP_VSPATH goto :no_vs

rem Prefer the CMake that ships with that Visual Studio: it always knows its
rem generator, while an older CMake on PATH may not.
set "COOP_VSCMAKE=%COOP_VSPATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
if exist "%COOP_VSCMAKE%\cmake.exe" set "PATH=%COOP_VSCMAKE%;%PATH%"
where cmake >nul 2>nul
if errorlevel 1 goto :no_cmake

rem VS 2022 by name; newer versions are what CMake picks by default.
set "COOP_GENERATOR_ARG="
if "%COOP_VSMAJOR%"=="17" set COOP_GENERATOR_ARG=-G "Visual Studio 17 2022"
echo Using Visual Studio %COOP_VSMAJOR% at %COOP_VSPATH%

set "COOP_SDKDIR=sdk"
if exist "sdk\inc\main.h" if exist "sdk\lib\ScriptHookRDR2.lib" goto :configure
rem A common slip: the zip's own folder ends up between sdk\ and inc\. Use it there.
for /d %%d in (sdk\*) do if exist "%%~d\inc\main.h" if exist "%%~d\lib\ScriptHookRDR2.lib" set "COOP_SDKDIR=%%~d"
if not "%COOP_SDKDIR%"=="sdk" echo Using the Script Hook RDR2 SDK in "%COOP_SDKDIR%"
if not "%COOP_SDKDIR%"=="sdk" goto :configure
echo.
echo NOTE: Script Hook RDR2 SDK not found, so only the test tools will be built.
echo       Extract the SDK so that sdk\inc\main.h and sdk\lib\ScriptHookRDR2.lib exist.
echo.

:configure
rem Absolute, with forward slashes: CMake keeps a relative -D path as given.
set "COOP_SDKPATH=%CD%\%COOP_SDKDIR%"
set "COOP_SDKPATH=%COOP_SDKPATH:\=/%"
cmake -S . -B build %COOP_GENERATOR_ARG% -A x64 "-DRDR2COOP_SDK_DIR:PATH=%COOP_SDKPATH%"
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
set "COOP_RESULT=0"
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
rem Keep the window open so a double-clicked run can be read. Claude Code (which
rem sets CLAUDECODE), CI, or "build.bat nopause" skip the wait.
if /i "%~1"=="nopause" exit /b %COOP_RESULT%
if defined CLAUDECODE exit /b %COOP_RESULT%
if defined CI exit /b %COOP_RESULT%
echo.
pause
exit /b %COOP_RESULT%
