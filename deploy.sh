#!/usr/bin/env bash
# Build, upload and monitor the current viewer app on the reTerminal.
#
# Usage (run from inside weather-viewer/, xkcd-viewer/ or photo-viewer/):
#   ../deploy.sh                    e1003, /dev/ttyUSB0 (defaults)
#   ../deploy.sh e1001              e1001, /dev/ttyUSB0
#   ../deploy.sh e1001 /dev/ttyUSB1 e1001, /dev/ttyUSB1
#
# On macOS the port is usually /dev/cu.usbserial-* -- pass it explicitly.
set -euo pipefail

board="${1:-e1003}"
env="reterminal_${board}"
port="${2:-/dev/ttyUSB0}"

if [ ! -f platformio.ini ]; then
    echo "[deploy] no platformio.ini in $(pwd)"
    echo "[deploy] cd into weather-viewer, xkcd-viewer or photo-viewer first"
    exit 1
fi

if ! command -v pio >/dev/null 2>&1; then
    echo "[deploy] pio not on PATH -- activate the PlatformIO env first"
    exit 1
fi

echo "[deploy] app=$(pwd)  env=${env}  port=${port}"
exec pio run -e "${env}" -t upload -t monitor \
    --upload-port "${port}" --monitor-port "${port}"
