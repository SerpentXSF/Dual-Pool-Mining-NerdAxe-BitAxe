@echo off
REM Launch the local USB flasher (Windows). Requires Python 3 on PATH.
cd /d "%~dp0"
python serve.py %*
pause
