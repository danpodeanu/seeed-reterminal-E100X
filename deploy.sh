#!/usr/bin/env bash
# Build, upload and monitor the current PlatformIO project on the reTerminal.
set -euo pipefail

script="$(basename "$0")"

usage() {
    cat <<EOF
Usage: ${script} <board> [port]

  board    e1001 | e1002 | e1003 | e1004 | e1005  (required)
  port     serial port for upload + monitor (default: auto-detect)

Examples:
  ../${script} e1003                  build + upload + monitor, auto-detect port
  ../${script} e1001 /dev/ttyUSB1     build + upload + monitor on /dev/ttyUSB1
  ../${script} e1001 /dev/cu.usbserial-1420   (macOS)

Auto-detect uses \`pio device list --json-output\` and picks the port when
exactly one USB serial device is present. If zero or more than one candidate
is found and no port was passed, the script errors out rather than guess.
Run from a project directory that defines the selected board environment.
EOF
}

if [ "$#" -lt 1 ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    usage
    exit 1
fi

board="$1"
case "${board}" in
    e1001|e1002|e1003|e1004|e1005) ;;
    *)
        echo "[deploy] error: unknown board \"${board}\"" >&2
        usage
        exit 1
        ;;
esac
port="${2:-}"

if [ ! -f platformio.ini ]; then
    echo "[deploy] error: no platformio.ini in $(pwd)" >&2
    echo "[deploy] cd into a viewer or hardware tool directory first." >&2
    exit 1
fi

env="reterminal_${board}"
if ! grep -Fqx "[env:${env}]" platformio.ini; then
    echo "[deploy] error: $(pwd) does not define PlatformIO environment ${env}." >&2
    exit 1
fi

if ! command -v pio >/dev/null 2>&1; then
    echo "[deploy] error: pio not on PATH -- activate the PlatformIO env first." >&2
    exit 1
fi

# Auto-detect a single USB serial device when the caller didn't name one.
# Shared logic with monitor.sh: filter pio's json output to entries whose
# hwid starts with USB (skips ACPI ports on Windows, Bluetooth on macOS)
# and require exactly one match before proceeding.
if [ -z "${port}" ]; then
    detected="$(pio device list --json-output 2>/dev/null | python3 -c '
import json, sys
try:
    ports = json.load(sys.stdin)
except Exception as exc:
    sys.stderr.write("could not parse pio device list output: {}".format(exc))
    sys.exit(0)
def looks_usb(p):
    hwid = (p.get("hwid") or "").upper()
    desc = (p.get("description") or "").lower()
    if not hwid.startswith("USB"):
        return False
    if "bluetooth" in desc or "debug-console" in desc:
        return False
    return True
matches = [p["port"] for p in ports if looks_usb(p)]
if len(matches) == 1:
    print(matches[0])
elif not matches:
    sys.stderr.write("no USB serial devices found")
else:
    sys.stderr.write("multiple USB serial candidates: {}".format(", ".join(matches)))
' 2>/tmp/deploy-detect.$$ || true)"
    reason="$(cat /tmp/deploy-detect.$$ 2>/dev/null || true)"
    rm -f /tmp/deploy-detect.$$
    if [ -n "${detected}" ]; then
        port="${detected}"
        echo "[deploy] auto-detected ${port}"
    else
        echo "[deploy] error: could not auto-detect port (${reason:-unknown reason})." >&2
        echo "[deploy] pass a port explicitly, e.g. \`${script} ${board} /dev/ttyUSB0\`." >&2
        exit 1
    fi
fi

echo "[deploy] app=$(pwd)  env=${env}  port=${port}"
exec pio run -e "${env}" -t upload -t monitor \
    --upload-port "${port}" --monitor-port "${port}"
