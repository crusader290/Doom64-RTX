@echo off
setlocal
rem Native Windows build; dependencies stay in build-deps\msys64.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\build_windows.ps1" %*
exit /b %errorlevel%
