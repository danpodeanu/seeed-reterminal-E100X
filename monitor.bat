@echo off
REM Attach a serial monitor to the reTerminal without touching flash.
REM Useful for tailing an already-running device (e.g. reading the SD
REM tee log line after a boot) without rebuilding or reflashing.
setlocal

set "SCRIPT=%~nx0"
set "PORT=%~1"
set "BAUD=%~2"
if "%PORT%"=="" set "PORT=COM3"
if "%BAUD%"=="" set "BAUD=115200"

if "%PORT%"=="/?" goto usage
if "%PORT%"=="-h" goto usage
if "%PORT%"=="--help" goto usage

where pio >nul 2>&1
if errorlevel 1 (
    echo [monitor] error: pio not on PATH -- activate the PlatformIO env first.
    exit /b 1
)

echo [monitor] port=%PORT%  baud=%BAUD%  (Ctrl-] to quit)
pio device monitor --port %PORT% --baud %BAUD%
exit /b %errorlevel%

:usage
echo Usage: %SCRIPT% [port] [baud]
echo.
echo   port     serial port to attach to (default: COM3)
echo   baud     baud rate                (default: 115200)
echo.
echo Examples:
echo   %SCRIPT%                  monitor COM3 @ 115200
echo   %SCRIPT% COM5             monitor COM5 @ 115200
echo   %SCRIPT% COM5 921600      monitor COM5 @ 921600
echo.
echo Ctrl-] exits the monitor. Runs from anywhere; no platformio.ini required.
exit /b 0
