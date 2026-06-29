@echo off
chcp 65001 >nul
python "%~dp0tools\flash_all.py" %*
pause
