@echo off
rem Build haisos for Windows on a Windows system
rem Usage: build_windows_on_windows.bat [debug]

setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
cd /d "%~dp0\.."
set "PROJECT_DIR=%cd%"

set "BUILD_TYPE=Release"
set "BUILD_SUFFIX="
set "HAISOS_DEBUG=OFF"
if "%~1"=="debug" (
    set "BUILD_TYPE=Debug"
    set "BUILD_SUFFIX=_debug"
    set "HAISOS_DEBUG=ON"
    echo Building haisos for Windows (Debug)
) else (
    echo Building haisos for Windows
)

cd /d "%PROJECT_DIR%"

rem Clone external dependencies if missing
if not exist "extern\nlohmann_json" (
    echo Cloning nlohmann/json
    git clone --branch v3.11.3 --depth 1 https://github.com/nlohmann/json.git extern/nlohmann_json
)

if not exist "extern\googletest" (
    echo Cloning google/googletest
    git clone --branch v1.14.0 --depth 1 https://github.com/google/googletest.git extern/googletest
)

rem Configure only on first run; reuse existing build system on rebuilds
if not exist "build\temp_windows%BUILD_SUFFIX%\CMakeCache.txt" (
    cmake -B build\temp_windows%BUILD_SUFFIX% -DHAISOS_DEBUG=%HAISOS_DEBUG%
)
cmake --build build\temp_windows%BUILD_SUFFIX% --config %BUILD_TYPE% --parallel %NUMBER_OF_PROCESSORS%

rem Print the final command to run
echo.
echo Build complete^! Run with:
echo %PROJECT_DIR%\output\windows%BUILD_SUFFIX%\haisos.exe --help
