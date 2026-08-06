@echo off
REM Build, upload and monitor the current PlatformIO project on the reTerminal.
setlocal EnableDelayedExpansion

set "SCRIPT=%~nx0"
set "BOARD=%~1"
set "PORT=%~2"

if "%BOARD%"=="/?" goto usage
if "%BOARD%"=="-h" goto usage
if "%BOARD%"=="--help" goto usage
if "%BOARD%"=="" goto usage

set "CANONICAL_BOARD="
if /I "%BOARD%"=="e1001" set "CANONICAL_BOARD=e1001"
if /I "%BOARD%"=="e1002" set "CANONICAL_BOARD=e1002"
if /I "%BOARD%"=="e1003" set "CANONICAL_BOARD=e1003"
if /I "%BOARD%"=="e1004" set "CANONICAL_BOARD=e1004"
if /I "%BOARD%"=="e1005" set "CANONICAL_BOARD=e1005"
if not defined CANONICAL_BOARD (
    echo [deploy] error: unknown board "%BOARD%"
    goto usage
)
set "BOARD=%CANONICAL_BOARD%"

if not exist platformio.ini (
    echo [deploy] error: no platformio.ini in %CD%
    echo [deploy] cd into a viewer or hardware tool directory first.
    exit /b 1
)

set "ENV=reterminal_%BOARD%"
set "ENV_FOUND="
for /f "usebackq delims=" %%L in ("platformio.ini") do (
    if /I "%%L"=="[env:!ENV!]" set "ENV_FOUND=1"
)
if not defined ENV_FOUND (
    echo [deploy] error: %CD% does not define PlatformIO environment %ENV%.
    exit /b 1
)

where pio >nul 2>&1
if errorlevel 1 (
    echo [deploy] error: pio not on PATH -- activate the PlatformIO env first.
    exit /b 1
)

REM Auto-detect a single USB serial device when the caller didn't name one.
REM Shared logic with monitor.bat -- see that script for details on the
REM OK#/ERR# protocol between the python helper and the batch parser.
if "%PORT%"=="" (
    set "DETECT="
    for /f "usebackq delims=" %%p in (`pio device list --json-output 2^>nul ^| python -c "import json,sys; d=json.load(sys.stdin); m=[p['port'] for p in d if (p.get('hwid') or '').upper().startswith('USB') and 'bluetooth' not in (p.get('description') or '').lower()]; print(('OK#'+m[0]) if len(m)==1 else (('ERR#no USB serial devices found') if not m else ('ERR#multiple candidates: '+', '.join(m))))"`) do set "DETECT=%%p"
    set "PREFIX=!DETECT:~0,3!"
    if "!PREFIX!"=="OK#" (
        set "PORT=!DETECT:~3!"
        echo [deploy] auto-detected !PORT!
    ) else (
        set "PREFIX4=!DETECT:~0,4!"
        if "!PREFIX4!"=="ERR#" (
            echo [deploy] error: could not auto-detect port ^(!DETECT:~4!^). 1>&2
        ) else (
            echo [deploy] error: could not auto-detect port ^(no output from pio device list^). 1>&2
        )
        echo [deploy] pass a port explicitly, e.g. `%SCRIPT% %BOARD% COM5`. 1>&2
        exit /b 1
    )
)

echo [deploy] app=%CD%  env=%ENV%  port=%PORT%
pio run -e %ENV% -t upload -t monitor --upload-port %PORT% --monitor-port %PORT%
exit /b %errorlevel%

:usage
echo Usage: %SCRIPT% ^<board^> [port]
echo.
echo   board    e1001 ^| e1002 ^| e1003 ^| e1004 ^| e1005  (required)
echo   port     serial port for upload + monitor (default: auto-detect)
echo.
echo Examples:
echo   ..\%SCRIPT% e1003              build + upload + monitor, auto-detect port
echo   ..\%SCRIPT% e1001 COM5         build + upload + monitor on COM5
echo.
echo Auto-detect uses `pio device list --json-output` and picks the port when
echo exactly one USB serial device is present. If zero or more than one
echo candidate is found and no port was passed, the script errors out rather
echo than guess.
echo.
echo Run from a project directory that defines the selected board environment.
exit /b 1
