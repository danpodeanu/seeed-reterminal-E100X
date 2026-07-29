#!/usr/bin/env bash
# Attach a serial monitor to the reTerminal without touching flash.
# Useful for tailing an already-running device (e.g. reading the SD
# tee log line after a boot) without rebuilding or reflashing.
set -euo pipefail

script="$(basename "$0")"

usage() {
    cat <<EOF
Usage: ${script} [port] [baud]

  port     serial port to attach to (default: /dev/ttyUSB0)
  baud     baud rate                (default: 115200)

Examples:
  ${script}                              monitor /dev/ttyUSB0 @ 115200
  ${script} /dev/ttyUSB1                 monitor /dev/ttyUSB1 @ 115200
  ${script} /dev/cu.usbserial-1420       (macOS)

Ctrl-] exits the monitor. Runs from anywhere; no platformio.ini required.
EOF
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    usage
    exit 0
fi

port="${1:-/dev/ttyUSB0}"
baud="${2:-115200}"

if ! command -v pio >/dev/null 2>&1; then
    echo "[monitor] error: pio not on PATH -- activate the PlatformIO env first." >&2
    exit 1
fi

echo "[monitor] port=${port}  baud=${baud}  (Ctrl-] to quit)"
exec pio device monitor --port "${port}" --baud "${baud}"
