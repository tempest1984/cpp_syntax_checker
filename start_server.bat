@echo off
title C++ Parser Studio Web Server
echo ========================================================
echo        Starting C++ Parser Studio Web Server...
echo ========================================================
echo.

where python >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Python is not found in your PATH.
    echo Please install Python 3.8+ or add it to PATH.
    pause
    exit /b 1
)

python run_server.py
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] Server exited with an error.
    pause
)

