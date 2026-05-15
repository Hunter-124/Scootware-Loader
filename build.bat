@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

call "%~dp0..\build\lib\env.bat"
if errorlevel 1 (
    if not "!SCOOTWARE_NO_PAUSE!"=="1" pause
    exit /b 1
)

call :run_build
set "ERR=!ERRORLEVEL!"
echo.
if not "%ERR%"=="0" (
    echo [-] Build failed ^(exit code %ERR%^).
) else (
    echo [+] Build finished OK.
    echo     Staged: %BIN%\scootware.exe, %BIN%\scootware-loader.exe
)
if not "%ERR%"=="0" if not "!SCOOTWARE_NO_PAUSE!"=="1" pause
exit /b %ERR%

:run_build
setlocal EnableExtensions EnableDelayedExpansion
set "SRCDIR=%CD%"
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

echo ==========================================
echo       Building Scootware Loader
echo ==========================================
echo.

echo [*] Generating CMake build files...
cmake -S . -B build
if errorlevel 1 (
    echo.
    echo [-] CMake generation failed! Ensure CMake is installed and on PATH.
    endlocal & exit /b 1
)

echo.
echo [*] Compiling ScootwareLoader...
cmake --build build --config Release --target ScootwareLoader
if errorlevel 1 (
    echo.
    echo [-] ScootwareLoader build failed!
    endlocal & exit /b 1
)

echo.
echo [*] Compiling hollow host - scootware.exe - RunPE / driver probe shell ...
cmake --build build --config Release --target ScootwareHost
if errorlevel 1 (
    echo.
    echo [-] ScootwareHost ^(scootware.exe^) build failed!
    endlocal & exit /b 1
)

if not exist "build\Release\scootware.exe" (
    echo.
    echo [-] Expected output missing: build\Release\scootware.exe
    echo     RunPE and driver bringup look for this file next to the loader.
    endlocal & exit /b 1
)

echo.
robocopy "%CD%\build\Release" "%BIN%" scootware.exe /NFL /NDL /NJH /NJS /nc /ns /np >nul
if errorlevel 8 (
    echo [-] robocopy failed copying scootware.exe to BIN.
    endlocal & exit /b 1
)
robocopy "%CD%\build\Release" "%BIN%" scootware-loader.exe /NFL /NDL /NJH /NJS /nc /ns /np >nul
if errorlevel 8 (
    echo [-] robocopy failed copying scootware-loader.exe to BIN.
    endlocal & exit /b 1
)

echo [+] Build successful!
echo     Loader:  build\Release\scootware-loader.exe
echo     Hollow:  build\Release\scootware.exe
endlocal & exit /b 0
