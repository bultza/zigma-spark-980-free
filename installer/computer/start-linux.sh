#!/bin/sh
set -eu
cd "$(dirname "$0")"
if ! command -v python3 >/dev/null 2>&1 || ! python3 -c 'import sys; assert sys.version_info >= (3,10)'; then
 echo 'Install Python 3.10+ with pip and venv support before continuing.' >&2
 exit 1
fi
if ! command -v nmcli >/dev/null 2>&1; then
 echo 'Install/enable NetworkManager (nmcli) and connect to your home Wi-Fi first.' >&2
 exit 1
fi
if [ ! -x .wizard-venv/bin/python ]; then
 python3 -m venv --without-pip .wizard-venv || { echo 'Install Python venv support using your distribution package manager.' >&2; exit 1; }
fi
if ! .wizard-venv/bin/python -c 'import paramiko, cryptography' 2>/dev/null; then
 if ! .wizard-venv/bin/python -m pip --version >/dev/null 2>&1; then
  echo 'Restoring pip in the installer environment...'
  if ! .wizard-venv/bin/python -m ensurepip --upgrade ||
     ! .wizard-venv/bin/python -m pip --version >/dev/null 2>&1; then
   echo 'Python could not bootstrap pip. On Debian/Ubuntu, run: sudo apt install python3-venv' >&2
   echo 'If you use a specific Python version, install its matching venv package, then rerun start-linux.sh.' >&2
   exit 1
  fi
 fi
 echo 'Preparing Python dependencies. Stay connected to your home Internet.'
 .wizard-venv/bin/python -m pip install -r requirements-wizard.txt
fi
exec .wizard-venv/bin/python wizard.py "$@"
