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
    echo [monitor] error: pio not on PATH -- activate the PlatformIO env first. 1>&2
    exit /b 1
)

REM Auto-detect a single USB serial device when the caller didn't name one.
REM The python helper emits either `OK#<port>` or `ERR#<reason>` on stdout so
REM the batch side can distinguish success from failure without inspecting
REM exit codes (the pipeline masks the python exit code anyway). We use `#`
REM as the separator to avoid needing to escape `|` inside the `for /f`
REM backtick command.
if "%PORT%"=="" (
    set "DETECT="
    for /f "usebackq delims=" %%p in (`pio device list --json-output 2^>nul ^| python -c "import json,sys; d=json.load(sys.stdin); m=[p['port'] for p in d if (p.get('hwid') or '').upper().startswith('USB') and 'bluetooth' not in (p.get('description') or '').lower()]; print(('OK#'+m[0]) if len(m)==1 else (('ERR#no USB serial devices found') if not m else ('ERR#multiple candidates: '+', '.join(m))))"`) do set "DETECT=%%p"
    set "PREFIX=!DETECT:~0,3!"
    if "!PREFIX!"=="OK#" (
        set "PORT=!DETECT:~3!"
        echo [monitor] auto-detected !PORT!
    ) else (
        set "PREFIX4=!DETECT:~0,4!"
        if "!PREFIX4!"=="ERR#" (
            echo [monitor] error: could not auto-detect port ^(!DETECT:~4!^). 1>&2
        ) else (
            echo [monitor] error: could not auto-detect port ^(no output from pio device list^). 1>&2
        )
        echo [monitor] pass a port explicitly, e.g. `%SCRIPT% COM5`. 1>&2
        exit /b 1
    )
)

echo [monitor] port=%PORT%  baud=%BAUD%  (Ctrl-] to quit)
pio device monitor --port %PORT% --baud %BAUD%
exit /b %errorlevel%

:usage
echo Usage: %SCRIPT% [port] [baud]
echo.
echo   port     serial port to attach to (default: auto-detect)
echo   baud     baud rate                (default: 115200)
echo.
echo Examples:
echo   %SCRIPT%                  auto-detect port @ 115200
echo   %SCRIPT% COM5             monitor COM5 @ 115200
echo   %SCRIPT% COM5 921600      monitor COM5 @ 921600
echo.
echo Auto-detect uses `pio device list --json-output` and picks the port when
echo exactly one USB serial device is present. If zero or more than one candidate
echo is found and no port was passed, the script errors out rather than guess.
echo Ctrl-] exits the monitor.
exit /b 0
