@echo off
setlocal EnableDelayedExpansion

set "REPO_ROOT=%~dp0.."

rem Read version (strip quotes)
for /f "usebackq delims=" %%a in ("%REPO_ROOT%\HAISOS_VERSION") do set "VERSION=%%~a"

rem Get branch name
if defined GITHUB_REF_NAME (
    set "BRANCH=%GITHUB_REF_NAME%"
) else (
    for /f "usebackq tokens=*" %%a in (`git -C "%REPO_ROOT%" rev-parse --abbrev-ref HEAD`) do set "BRANCH=%%a"
)

rem Extract branch name: remove before first slash, keep before second slash
for /f "tokens=1* delims=/" %%a in ("%BRANCH%") do set "REST=%%b"
if not defined REST set "REST=%BRANCH%"
for /f "tokens=1 delims=/" %%a in ("%REST%") do set "BRANCH_NAME=%%a"

rem Get full git hash and first 15 chars
for /f "usebackq tokens=*" %%a in (`git -C "%REPO_ROOT%" rev-parse HEAD`) do set "GIT_HASH=%%a"
set "GIT_HASH_SHORT=!GIT_HASH:~0,15!"

rem Get base branch
if defined GITHUB_BASE_REF (
    set "BASE_BRANCH=%GITHUB_BASE_REF%"
) else (
    for /f "usebackq tokens=*" %%a in (`git -C "%REPO_ROOT%" symbolic-ref refs/remotes/origin/HEAD 2^>nul`) do (
        set "DEFAULT_REF=%%a"
    )
    if defined DEFAULT_REF (
        set "BASE_BRANCH=!DEFAULT_REF:refs/remotes/origin/=!"
    ) else (
        set "BASE_BRANCH=unknown"
    )
)

rem Compute minute of year using PowerShell
for /f "usebackq tokens=*" %%a in (`powershell -NoProfile -Command "$d = Get-Date; $min = ($d.DayOfYear - 1) * 1440 + $d.Hour * 60 + $d.Minute; Write-Host $min"`) do set "MIN_OF_YEAR=%%a"

rem Build zip name
set "ZIP_NAME=haisos.%BRANCH_NAME%.%VERSION%.%MIN_OF_YEAR%.%GIT_HASH_SHORT%.zip"

rem Create staging directory
set "STAGING=%TEMP%\haisos_pack_%RANDOM%"
mkdir "%STAGING%\bin\linux"
mkdir "%STAGING%\bin\windows"
mkdir "%STAGING%\meta"

rem Copy binaries (ignore errors)
xcopy /E /I /Y "%REPO_ROOT%\output\linux\*" "%STAGING%\bin\linux\" >nul 2>&1
xcopy /E /I /Y "%REPO_ROOT%\output\windows\*" "%STAGING%\bin\windows\" >nul 2>&1

rem Compute GitHub commit URL
if defined GITHUB_SERVER_URL (
    if defined GITHUB_REPOSITORY (
        set "GITHUB_COMMIT_URL=%GITHUB_SERVER_URL%/%GITHUB_REPOSITORY%/commit/%GIT_HASH%"
    ) else (
        set "GITHUB_COMMIT_URL=https://github.com/paul-neata/haisos/commit/%GIT_HASH%"
    )
) else if defined GITHUB_REPOSITORY (
    set "GITHUB_COMMIT_URL=https://github.com/%GITHUB_REPOSITORY%/commit/%GIT_HASH%"
) else (
    set "GITHUB_COMMIT_URL=https://github.com/paul-neata/haisos/commit/%GIT_HASH%"
)

rem Write meta files
echo https://github.com/paul-neata/haisos > "%STAGING%\meta\repo"
echo %BRANCH% > "%STAGING%\meta\branch"
echo %GIT_HASH% > "%STAGING%\meta\git_hash"
echo %VERSION% > "%STAGING%\meta\version"
echo %BASE_BRANCH% > "%STAGING%\meta\base_branch"
echo %GITHUB_COMMIT_URL% > "%STAGING%\meta\github_commit_url"

rem Copy LICENSE
copy /Y "%REPO_ROOT%\LICENSE" "%STAGING%\LICENSE" >nul

rem Create zip
powershell -NoProfile -Command "Compress-Archive -Path '%STAGING%\*' -DestinationPath '%REPO_ROOT%\%ZIP_NAME%' -Force"

echo Created: %ZIP_NAME%

rem Cleanup
rmdir /S /Q "%STAGING%"
