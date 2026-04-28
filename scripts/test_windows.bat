@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0\.."
set "PROJECT_DIR=%cd%"
set "PLATFORM_ARG=%~1"
set "SELECTOR_ARG=%~2"
if "%PLATFORM_ARG%"=="" set "PLATFORM_ARG=W"
if "%SELECTOR_ARG%"=="" set "SELECTOR_ARG=*"
shift
shift
set "CONFIGS=release"
set "FILTER="
:parse_args
if "%~1"=="" goto :done_parse
if "%~1"=="--debug" (
    set "CONFIGS=debug"
    shift
    goto :parse_args
)
if "%~1"=="--both" (
    set "CONFIGS=both"
    shift
    goto :parse_args
)
if "%FILTER%"=="" (
    set "FILTER=%~1"
) else (
    set "FILTER=%FILTER% %~1"
)
shift
goto :parse_args
:done_parse
set "PLATFORMS="
set "TMP=%PLATFORM_ARG%"
if /I not "%TMP%"=="%TMP:*W=%" set "PLATFORMS=%PLATFORMS% W"
if /I not "%TMP%"=="%TMP:*L=%" set "PLATFORMS=%PLATFORMS% L"
if /I not "%TMP%"=="%TMP:*N=%" set "PLATFORMS=%PLATFORMS% N"
if /I "%PLATFORM_ARG%"=="*" set "PLATFORMS=W L N"
set "TESTS="
set "TMP=%SELECTOR_ARG%"
if /I not "%TMP%"=="%TMP:*U=%" set "TESTS=%TESTS% U"
if /I not "%TMP%"=="%TMP:*I=%" set "TESTS=%TESTS% I"
if /I not "%TMP%"=="%TMP:*H=%" set "TESTS=%TESTS% H"
if /I "%SELECTOR_ARG%"=="*" set "TESTS=U I H"
set "CONFIG_LIST="
if "%CONFIGS%"=="both" (
    set "CONFIG_LIST=release debug"
) else if "%CONFIGS%"=="debug" (
    set "CONFIG_LIST=debug"
) else (
    set "CONFIG_LIST=release"
)
set "ANY_FAILED=0"
for %%P in (%PLATFORMS%) do (
    if "%%P"=="L" (
        echo(
        echo L
    ) else if "%%P"=="N" (
        echo(
        echo N
    ) else if "%%P"=="W" (
        for %%C in (%CONFIG_LIST%) do (
            call :run_windows_tests %%C
        )
    )
)
if "%ANY_FAILED%"=="1" (
    exit /b 1
) else (
    exit /b 0
)
:run_windows_tests
set "config=%~1"
set "build_suffix="
if "%config%"=="debug" (
    set "build_suffix=_debug"
)
set "output_dir=%PROJECT_DIR%\output\windows%build_suffix%"
set "unit_passed=0"
set "unit_total=0"
set "unit_failed_names=none"
set "int_passed=0"
set "int_total=0"
set "int_failed_names=none"
set "haisos_passed=0"
set "haisos_total=0"
set "haisos_failed_names=none"
set "TMP=%TESTS%"
if /I not "%TMP%"=="%TMP:*U=%" (
    for %%F in (%output_dir%\*.unittests.exe) do (
        set "name=%%~nxF"
        set "run_test=1"
        if not "%FILTER%"=="" (
            set "TMPNAME=!name!"
            set "HAS_FILTER=0"
            if /I not "!TMPNAME!"=="!TMPNAME:*%FILTER%=!" set "HAS_FILTER=1"
            if "!HAS_FILTER!"=="0" set "run_test=0"
        )
        if "!run_test!"=="1" (
            set /a unit_total+=1
            echo(
            echo Running: %%F
            set "gtest_args="
            if not "%FILTER%"=="" (
                set "gtest_args=--gtest_filter=*%FILTER%*"
            )
            %%F !gtest_args!
            if errorlevel 1 (
                if "!unit_failed_names!"=="none" (
                    set "unit_failed_names=!name!"
                ) else (
                    set "unit_failed_names=!unit_failed_names! !name!"
                )
                set "ANY_FAILED=1"
            ) else (
                set /a unit_passed+=1
            )
        )
    )
)
set "TMP=%TESTS%"
if /I not "%TMP%"=="%TMP:*I=%" (
    for %%F in (%output_dir%\*.integrationtest.exe) do (
        set "name=%%~nxF"
        set "run_test=1"
        if not "%FILTER%"=="" (
            set "TMPNAME=!name!"
            set "HAS_FILTER=0"
            if /I not "!TMPNAME!"=="!TMPNAME:*%FILTER%=!" set "HAS_FILTER=1"
            if "!HAS_FILTER!"=="0" set "run_test=0"
        )
        if "!run_test!"=="1" (
            set /a int_total+=1
            %%F
            if errorlevel 1 (
                if "!int_failed_names!"=="none" (
                    set "int_failed_names=!name!"
                ) else (
                    set "int_failed_names=!int_failed_names! !name!"
                )
                set "ANY_FAILED=1"
            ) else (
                set /a int_passed+=1
            )
        )
    )
)
set "TMP=%TESTS%"
if /I not "%TMP%"=="%TMP:*H=%" (
    for %%F in (%PROJECT_DIR%\tests\haisos\*.haisostest\*.haisostest.js) do (
        set "name=%%~nxF"
        set "run_test=1"
        if not "%FILTER%"=="" (
            set "TMPNAME=!name!"
            set "HAS_FILTER=0"
            if /I not "!TMPNAME!"=="!TMPNAME:*%FILTER%=!" set "HAS_FILTER=1"
            if "!HAS_FILTER!"=="0" set "run_test=0"
        )
        if "!run_test!"=="1" (
            set /a haisos_total+=1
            echo(
            echo Running: node %%F
            node "%%F"
            if errorlevel 1 (
                if "!haisos_failed_names!"=="none" (
                    set "haisos_failed_names=!name!"
                ) else (
                    set "haisos_failed_names=!haisos_failed_names! !name!"
                )
                set "ANY_FAILED=1"
            ) else (
                set /a haisos_passed+=1
            )
        )
    )
)
echo(
echo ========================================
echo Windows %config% tests
echo ========================================
set "TMP=%TESTS%"
if /I not "%TMP%"=="%TMP:*U=%" (
    if not "%unit_failed_names%"=="none" (
        echo %unit_passed%/%unit_total% unit tests pass ^| FAILED: %unit_failed_names%
    ) else (
        echo %unit_passed%/%unit_total% unit tests pass
    )
) else (
    echo -/- unit tests pass
)
if /I not "%TMP%"=="%TMP:*I=%" (
    if not "%int_failed_names%"=="none" (
        echo %int_passed%/%int_total% integration tests pass ^| FAILED: %int_failed_names%
    ) else (
        echo %int_passed%/%int_total% integration tests pass
    )
) else (
    echo -/- integration tests pass
)
if /I not "%TMP%"=="%TMP:*H=%" (
    if not "%haisos_failed_names%"=="none" (
        echo %haisos_passed%/%haisos_total% haisos tests pass ^| FAILED: %haisos_failed_names%
    ) else (
        echo %haisos_passed%/%haisos_total% haisos tests pass
    )
) else (
    echo -/- haisos tests pass
)
echo ========================================
goto :eof
