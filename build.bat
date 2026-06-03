@echo off
setlocal

echo ========== Building DuiLib Finder ==========

:: Create build directory
if not exist build mkdir build

cd /d "%~dp0build"

:: Configure CMake - detect cmake in common locations
set CMAKE_PATH=
if exist "D:\install\cmake\bin\cmake.exe" set CMAKE_PATH=D:\install\cmake\bin\cmake.exe
if exist "D:\install\vs2017\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set CMAKE_PATH=D:\install\vs2017\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
if exist "F:\cmake-4.2.0-windows-x86_64\bin\cmake.exe" set CMAKE_PATH=F:\cmake-4.2.0-windows-x86_64\bin\cmake.exe

if "%CMAKE_PATH%"=="" (
    echo ERROR: CMake not found! Please install CMake or add to PATH.
    pause
    exit /b 1
)

echo [1/3] Configuring CMake...
"%CMAKE_PATH%" -G "Visual Studio 17 2022" -A Win32 ".."
if %errorlevel% neq 0 (
    echo ERROR: CMake configuration failed!
    pause
    exit /b 1
)

:: Build
echo [2/3] Building...
"%CMAKE_PATH%" --build . --config Release
if %errorlevel% neq 0 (
    echo ERROR: Build failed!
    pause
    exit /b 1
)

echo [3/3] Build complete!
echo.

:: Show output files
echo Output files:
dir /s /b ..\bin\*.exe 2>nul
dir /s /b ..\bin\*.dll 2>nul

echo.
echo Done!
pause