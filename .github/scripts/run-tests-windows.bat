@echo off
setlocal EnableDelayedExpansion

set "CACHE_DB=tests\tool\llm_cache_proxy_database"
set "PROXY_LOG=proxy.log"
set "PROXY_PORT=11435"

rem Always run unit tests
echo Running unit tests...
scripts\run_tests_windows.bat W U

rem Check if cache database has recordings
set "HAS_RECORDINGS=0"
if exist "%CACHE_DB%" (
    for %%f in ("%CACHE_DB%\*.response.json") do set "HAS_RECORDINGS=1"
)

rem Run integration and haisos tests only if recordings exist
if "%HAS_RECORDINGS%"=="1" (
    echo Cache database found. Starting LLM cache proxy in serve mode...
    start /B node tests\tools\llm_cache_proxy\src\index.js --serve --folder "%CACHE_DB%" --port %PROXY_PORT% --log-file "%PROXY_LOG%"
    for /l %%i in (1,1,30) do (
        curl -s http://localhost:%PROXY_PORT%/ > nul 2>&1
        if !errorlevel! equ 0 (
            echo Proxy ready
            goto :proxy_ready
        )
        timeout /t 1 > nul
    )
    :proxy_ready
    set "HAISOS_ENDPOINT=http://localhost:%PROXY_PORT%/api/chat"

    echo Running integration tests...
    scripts\run_tests_windows.bat W I

    echo Running haisos tests...
    scripts\run_tests_windows.bat W H

    rem Note: proxy process is left running; GitHub Actions will clean it up
) else (
    echo No cache database recordings found. Skipping integration and haisos tests.
)
