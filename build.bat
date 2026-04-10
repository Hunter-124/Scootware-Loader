@echo off
echo ==========================================
echo       Building Scootware Loader           
echo ==========================================
echo.

if not exist build (
    mkdir build
)
cd build

echo [*] Generating CMake build files...
cmake ..
if %errorlevel% neq 0 (
    echo.
    echo [-] CMake generation failed! Ensure CMake is installed.
    pause
    exit /b %errorlevel%
)

echo.
echo [*] Compiling project...
cmake --build . --config Release
if %errorlevel% neq 0 (
    echo.
    echo [-] Compilation failed!
    pause
    exit /b %errorlevel%
)

echo.
echo [+] Build successful! Executable is located in build\Release
pause
