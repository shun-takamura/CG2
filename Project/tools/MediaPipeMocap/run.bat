@echo off
cd /d "%~dp0"

where py >nul 2>nul
if %errorlevel%==0 (
    py -3 main.py
    goto :check
)

where python >nul 2>nul
if %errorlevel%==0 (
    python main.py
    goto :check
)

echo Python not found. Please install Python 3 and try again.
pause
goto :eof

:check
echo.
echo Exit code: %errorlevel%
pause
