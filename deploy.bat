@echo off
REM Build, upload and monitor the current viewer app on the reTerminal.
REM
REM Usage (run from inside weather-viewer\, xkcd-viewer\ or photo-viewer\):
REM   ..\deploy.bat                e1003, COM3 (defaults)
REM   ..\deploy.bat e1001          e1001, COM3
REM   ..\deploy.bat e1001 COM5     e1001, COM5
setlocal

set "BOARD=%~1"
if "%BOARD%"=="" set "BOARD=e1003"
set "ENV=reterminal_%BOARD%"

set "PORT=%~2"
if "%PORT%"=="" set "PORT=COM3"

if not exist platformio.ini (
    echo [deploy] no platformio.ini in %CD%
    echo [deploy] cd into weather-viewer, xkcd-viewer or photo-viewer first
    exit /b 1
)

where pio >nul 2>&1
if errorlevel 1 (
    echo [deploy] pio not on PATH -- activate the PlatformIO env first
    exit /b 1
)

echo [deploy] app=%CD%  env=%ENV%  port=%PORT%
pio run -e %ENV% -t upload -t monitor --upload-port %PORT% --monitor-port %PORT%
exit /b %errorlevel%
