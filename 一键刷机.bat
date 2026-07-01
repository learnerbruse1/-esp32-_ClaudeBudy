@echo off
setlocal enabledelayedexpansion

:: ============================================================
::  XiaoMiao Buddy - One-Click Flash Tool
::  Pure ASCII launcher -> delegates to Python for UTF-8 output
:: ============================================================

set "SCRIPT_DIR=%~dp0"
set "ASSEMBLE_PY=%SCRIPT_DIR%tools\assemble_image.py"
set "FLASH_PY=%SCRIPT_DIR%tools\flash_all.py"
set "IMG=%SCRIPT_DIR%flash_unified.bin"

:: ---- Check Python (version 3.7+) ----
where python >nul 2>&1
if %errorlevel% neq 0 (
    echo.
    echo   [FAIL] Python not found in PATH.
    echo.
    echo   Please install Python 3.8+ and check "Add Python to PATH":
    echo     https://www.python.org/downloads/
    echo.
    pause
    exit /b 1
)
for /f "tokens=2 delims= " %%v in ('python --version 2^>^&1') do set "PY_MAJOR=%%v"
for /f "tokens=1 delims=." %%m in ("!PY_MAJOR!") do set "PY_MAJOR=%%m"
if !PY_MAJOR! lss 3 (
    echo.
    echo   [FAIL] Python 3 required, but found Python !PY_MAJOR!.
    echo.
    echo   Please install Python 3.8+:
    echo     https://www.python.org/downloads/
    echo.
    pause
    exit /b 1
)

:: ---- Check esptool ----
python -m esptool version >nul 2>&1
if %errorlevel% neq 0 (
    echo.
    echo   [FAIL] esptool is not available.
    echo.
    echo   Possible causes:
    echo     1. esptool not installed -> run: pip install esptool
    echo     2. Wrong Python environment -> check: pip list ^| findstr esptool
    echo     3. Python version too old -> need Python 3.8+
    echo.
    pause
    exit /b 2
)

:: ---- Assemble image if missing ----
if not exist "!IMG!" (
    echo.
    echo   [*] flash_unified.bin not found, assembling...
    echo.
    python "!ASSEMBLE_PY!" --yes
    if !errorlevel! neq 0 (
        echo.
        echo   [FAIL] Image assembly failed.
        echo   Please run "idf.py build" in ESP-IDF environment first.
        echo.
        pause
        exit /b 3
    )
    if not exist "!IMG!" (
        echo   [FAIL] Assembly completed but image still missing.
        pause
        exit /b 3
    )
)

:: ---- Run flash_all.py (handles UTF-8 output internally) ----
python "%FLASH_PY%" %*
set "FLASH_RESULT=%errorlevel%"

echo.
pause
endlocal
exit /b !FLASH_RESULT!
