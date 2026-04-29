@echo off
node "%~dp0internal\test.js" %*
exit /b %ERRORLEVEL%
