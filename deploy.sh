#!/usr/bin/env bash
# Build, upload and monitor the current viewer app on the reTerminal.
set -euo pipefail

script="$(basename "$0")"

usage() {
    cat <<EOF
Usage: ${script} <board> [port]

  board    e1001 | e1002 | e1003 | e1004  (required)
  port     serial port for upload + monitor (default: /dev/ttyUSB0)

Examples:
  ../${script} e1003                  build + upload + monitor on /dev/ttyUSB0
  ../${script} e1001 /dev/ttyUSB1     build + upload + monitor on /dev/ttyUSB1
  ../${script} e1001 /dev/cu.usbserial-1420   (macOS)

Run from inside weather-viewer/, xkcd-viewer/ or photo-viewer/.
EOF
}

if [ "$#" -lt 1 ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    usage
    exit 1
fi

board="$1"
case "${board}" in
    e1001|e1002|e1003|e1004) ;;
    *)
        echo "[deploy] error: unknown board \"${board}\"" >&2
        usage
        exit 1
        ;;
esac
port="${2:-/dev/ttyUSB0}"

if [ ! -f platformio.ini ]; then
    echo "[deploy] error: no platformio.ini in $(pwd)" >&2
    echo "[deploy] cd into weather-viewer, xkcd-viewer or photo-viewer first." >&2
    exit 1
fi

if ! command -v pio >/dev/null 2>&1; then
    echo "[deploy] error: pio not on PATH -- activate the PlatformIO env first." >&2
    exit 1
fi

env="reterminal_${board}"
echo "[deploy] app=$(pwd)  env=${env}  port=${port}"
exec pio run -e "${env}" -t upload -t monitor \
    --upload-port "${port}" --monitor-port "${port}"
