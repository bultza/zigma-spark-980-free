@echo off
rem Keep a dedicated console alive even if the worker fails before Python starts.
start "Zigma Free installer" "%ComSpec%" /d /k ""%~dp0run-windows.cmd" %*"
if errorlevel 1 goto launch_failed
exit /b 0
:launch_failed
echo Could not open the installer console. Extract the complete ZIP first.
echo Open Command Prompt in the extracted folder and run run-windows.cmd
pause
exit /b 1
