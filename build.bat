@echo off
setlocal EnableExtensions
cd /d "%~dp0"

echo ==========================================
echo       Building Scootware Loader           
echo ==========================================
echo.

:: Normalize source dir to match CMAKE_HOME_DIRECTORY in CMakeCache (forward slashes, no trailing slash)
set "SRCDIR=%~dp0"
if "%SRCDIR:~-1%"=="\" set "SRCDIR=%SRCDIR:~0,-1%"
set "SRCDIR=%SRCDIR:\=/%"

if exist "build\CMakeCache.txt" (
    findstr /I /L /C:"CMAKE_HOME_DIRECTORY:INTERNAL=%SRCDIR%" "build\CMakeCache.txt" >nul 2>&1
    if errorlevel 1 (
        echo [!] Stale CMake cache: build was configured for a different source folder.
        echo     Removing build folder so CMake can reconfigure for this project...
        rmdir /s /q "build" 2>nul
    )
)

if not exist "build" mkdir "build"

echo [*] Generating CMake build files...
cmake -S . -B build
if %errorlevel% neq 0 (
    echo.
    echo [-] CMake generation failed! Ensure CMake is installed.
    pause
    exit /b %errorlevel%
)

echo.
echo [*] Compiling project...
cmake --build build --config Release
if %errorlevel% neq 0 (
    echo.
    echo [-] Compilation failed!
    pause
    exit /b %errorlevel%
)

echo.
echo [+] Build successful! Executable is located in build\Release
pause
endlocal
exit /b 0
