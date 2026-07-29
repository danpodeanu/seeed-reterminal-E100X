#!/usr/bin/env bash
# Attach a serial monitor to the reTerminal without touching flash.
# Useful for tailing an already-running device (e.g. reading the SD
# tee log line after a boot) without rebuilding or reflashing.
set -euo pipefail

script="$(basename "$0")"

usage() {
    cat <<EOF
Usage: ${script} [port] [baud]

  port     serial port to attach to (default: auto-detect, fallback /dev/ttyUSB0)
  baud     baud rate                (default: 115200)

Examples:
  ${script}                              auto-detect port @ 115200
  ${script} /dev/ttyUSB1                 monitor /dev/ttyUSB1 @ 115200
  ${script} /dev/cu.usbserial-1420       (macOS)

Auto-detect uses \`pio device list --json-output\` and picks the port when
exactly one USB serial device is present, so plug the reTerminal in on its
own and no port argument is needed. Ctrl-] exits the monitor.
EOF
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    usage
    exit 0
fi

if ! command -v pio >/dev/null 2>&1; then
    echo "[monitor] error: pio not on PATH -- activate the PlatformIO env first." >&2
    exit 1
fi

port="${1:-}"
baud="${2:-115200}"

# Try to auto-detect a single USB serial device when the caller didn't
# name one. Any parse failure or ambiguous result just falls back to the
# platform default, so this stays a best-effort convenience.
if [ -z "${port}" ]; then
    detected="$(pio device list --json-output 2>/dev/null | python3 -c '
import json, sys
try:
    ports = json.load(sys.stdin)
except Exception:
    sys.exit(0)
def looks_usb(p):
    hwid = (p.get("hwid") or "").upper()
    desc = (p.get("description") or "").lower()
    if not hwid.startswith("USB"):
        return False
    # Skip well-known noise on macOS.
    if "bluetooth" in desc or "debug-console" in desc:
        return False
    return True
matches = [p["port"] for p in ports if looks_usb(p)]
if len(matches) == 1:
    print(matches[0])
' 2>/dev/null || true)"
    if [ -n "${detected}" ]; then
        port="${detected}"
        echo "[monitor] auto-detected ${port}"
    else
        port="/dev/ttyUSB0"
        echo "[monitor] auto-detect: 0 or >1 candidates; falling back to ${port}" >&2
    fi
fi

echo "[monitor] port=${port}  baud=${baud}  (Ctrl-] to quit)"
exec pio device monitor --port "${port}" --baud "${baud}"
