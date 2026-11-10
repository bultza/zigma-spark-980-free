@echo off
setlocal
cd /d "%~dp0"
echo [%date% %time%] Zigma Free installer - preparing Python. This window stays open on errors.
if not exist "wizard.py" goto not_extracted
py -3 -c "import sys; assert sys.version_info >= (3,10)" >nul 2>&1
if not errorlevel 1 goto use_launcher
python -c "import sys; assert sys.version_info >= (3,10)" >nul 2>&1
if not errorlevel 1 goto use_python
echo [%date% %time%] Python 3.10+ is missing. Install it from https://www.python.org/downloads/windows/
echo Enable the Python launcher or Add Python to PATH, then reopen start-windows.cmd.
exit /b 1
:use_launcher
set "ZIGMA_PY=py -3"
goto prepare
:use_python
set "ZIGMA_PY=python"
:prepare
if exist ".wizard-venv\Scripts\python.exe" goto dependencies
echo [%date% %time%] Creating an isolated Python environment...
%ZIGMA_PY% -m venv .wizard-venv
if errorlevel 1 goto failed
:dependencies
".wizard-venv\Scripts\python.exe" -c "import paramiko, cryptography" >nul 2>&1
if not errorlevel 1 goto wizard
echo [%date% %time%] Installing dependencies. Stay connected to home Internet.
".wizard-venv\Scripts\python.exe" -m pip install -r "requirements-wizard.txt"
if errorlevel 1 goto failed
:wizard
echo [%date% %time%] Starting the assistant...
".wizard-venv\Scripts\python.exe" -u "wizard.py" %*
if errorlevel 1 goto failed
echo [%date% %time%] Assistant finished. You may close this window.
exit /b 0
:not_extracted
echo [%date% %time%] Missing wizard.py. Extract ALL ZIP contents before running.
exit /b 1
:failed
echo [%date% %time%] Setup stopped. Read the error above. Do not resend pairing blindly.
exit /b 1
