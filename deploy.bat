@echo off
REM Build, upload and monitor the current viewer app on the reTerminal.
setlocal

set "SCRIPT=%~nx0"
set "BOARD=%~1"
set "PORT=%~2"
if "%PORT%"=="" set "PORT=COM3"

set "VALID=0"
if /I "%BOARD%"=="e1001" set "VALID=1"
if /I "%BOARD%"=="e1002" set "VALID=1"
if /I "%BOARD%"=="e1003" set "VALID=1"
if /I "%BOARD%"=="e1004" set "VALID=1"

if "%BOARD%"=="/?" goto usage
if "%BOARD%"=="-h" goto usage
if "%BOARD%"=="--help" goto usage
if "%BOARD%"=="" goto usage
if "%VALID%"=="0" (
    echo [deploy] error: unknown board "%BOARD%"
    goto usage
)

if not exist platformio.ini (
    echo [deploy] error: no platformio.ini in %CD%
    echo [deploy] cd into weather-viewer, xkcd-viewer or photo-viewer first.
    exit /b 1
)

where pio >nul 2>&1
if errorlevel 1 (
    echo [deploy] error: pio not on PATH -- activate the PlatformIO env first.
    exit /b 1
)

set "ENV=reterminal_%BOARD%"
echo [deploy] app=%CD%  env=%ENV%  port=%PORT%
pio run -e %ENV% -t upload -t monitor --upload-port %PORT% --monitor-port %PORT%
exit /b %errorlevel%

:usage
echo Usage: %SCRIPT% ^<board^> [port]
echo.
echo   board    e1001 ^| e1002 ^| e1003 ^| e1004  (required)
echo   port     serial port for upload + monitor (default: COM3)
echo.
echo Examples:
echo   ..\%SCRIPT% e1003              build + upload + monitor on COM3
echo   ..\%SCRIPT% e1001 COM5         build + upload + monitor on COM5
echo.
echo Run from inside weather-viewer\, xkcd-viewer\ or photo-viewer\.
exit /b 1
