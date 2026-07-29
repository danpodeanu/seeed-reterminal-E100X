@echo off
REM Attach a serial monitor to the reTerminal without touching flash.
REM Useful for tailing an already-running device (e.g. reading the SD
REM tee log line after a boot) without rebuilding or reflashing.
setlocal EnableDelayedExpansion

set "SCRIPT=%~nx0"
set "PORT=%~1"
set "BAUD=%~2"
if "%BAUD%"=="" set "BAUD=115200"

if "%PORT%"=="/?" goto usage
if "%PORT%"=="-h" goto usage
if "%PORT%"=="--help" goto usage

where pio >nul 2>&1
if errorlevel 1 (
    echo [monitor] error: pio not on PATH -- activate the PlatformIO env first.
    exit /b 1
)

REM Auto-detect a single USB serial device when the caller didn't name one.
REM Any ambiguity or parse failure just falls back to COM3.
if "%PORT%"=="" (
    for /f "usebackq delims=" %%p in (`pio device list --json-output 2^>nul ^| python -c "import json,sys; d=json.load(sys.stdin); m=[p['port'] for p in d if (p.get('hwid') or '').upper().startswith('USB') and 'bluetooth' not in (p.get('description') or '').lower()]; print(m[0] if len(m)==1 else '')" 2^>nul`) do set "PORT=%%p"
    if "!PORT!"=="" (
        set "PORT=COM3"
        echo [monitor] auto-detect: 0 or ^>1 candidates; falling back to !PORT!
    ) else (
        echo [monitor] auto-detected !PORT!
    )
)

echo [monitor] port=%PORT%  baud=%BAUD%  (Ctrl-] to quit)
pio device monitor --port %PORT% --baud %BAUD%
exit /b %errorlevel%

:usage
echo Usage: %SCRIPT% [port] [baud]
echo.
echo   port     serial port to attach to (default: auto-detect, fallback COM3)
echo   baud     baud rate                (default: 115200)
echo.
echo Examples:
echo   %SCRIPT%                  auto-detect port @ 115200
echo   %SCRIPT% COM5             monitor COM5 @ 115200
echo   %SCRIPT% COM5 921600      monitor COM5 @ 921600
echo.
echo Auto-detect uses `pio device list --json-output` and picks the port when
echo exactly one USB serial device is present, so plug the reTerminal in on its
echo own and no port argument is needed. Ctrl-] exits the monitor.
exit /b 0
