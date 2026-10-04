@echo off
setlocal EnableDelayedExpansion

set "CACHE_DB=tests\tool\llm_cache_proxy_database"
set "PROXY_LOG=proxy.log"
set "PROXY_PORT=11435"

rem Always run unit tests. "call" returns here afterwards; the exit status of
rem every run is checked, so a failure fails the job.
echo Running unit tests...
call scripts\test_windows.bat W U
if errorlevel 1 exit /b 1

rem Check if cache database has recordings
set "HAS_RECORDINGS=0"
if exist "%CACHE_DB%" (
    for %%f in ("%CACHE_DB%\*.response.json") do set "HAS_RECORDINGS=1"
)

rem Run integration and haisos tests only if recordings exist
if not "%HAS_RECORDINGS%"=="1" (
    echo No cache database recordings found. Skipping integration and haisos tests.
    exit /b 0
)

echo Cache database found. Starting LLM cache proxy in serve mode...
start /B node tests\tools\llm_cache_proxy\src\index.js --serve --folder "%CACHE_DB%" --port %PROXY_PORT% --log-file "%PROXY_LOG%"
for /l %%i in (1,1,30) do (
    curl -s http://localhost:%PROXY_PORT%/ > nul 2>&1
    if !errorlevel! equ 0 goto :proxy_ready
    ping -n 2 127.0.0.1 > nul
)
:proxy_ready
echo Proxy ready
set "HAISOS_ENDPOINT=http://localhost:%PROXY_PORT%/api/chat"

echo Running integration tests...
call scripts\test_windows.bat W I
if errorlevel 1 exit /b 1

echo Running haisos tests...
call scripts\test_windows.bat W H
if errorlevel 1 exit /b 1

rem Note: proxy process is left running; GitHub Actions will clean it up
exit /b 0
