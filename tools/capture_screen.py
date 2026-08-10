#!/usr/bin/env python3
"""Retrieve the current reTerminal framebuffer as a BMP over USB CDC."""

from __future__ import annotations

import argparse
import binascii
import re
import struct
import sys
import time
from pathlib import Path


PROTOCOL = "RETERMINAL_SCREEN_CAPTURE_V1"
REQUEST = f"{PROTOCOL}\n".encode("ascii")
OK_PATTERN = re.compile(
    rb"^RETERMINAL_SCREEN_CAPTURE_V1 OK ([0-9]+) ([0-9]+) ([0-9]+)\n$"
)
END_PATTERN = re.compile(
    rb"^RETERMINAL_SCREEN_CAPTURE_V1 END ([0-9A-Fa-f]{8})\n$"
)
ERROR_PREFIX = f"{PROTOCOL} ERROR ".encode("ascii")
BMP_PIXEL_OFFSET = 1078
MAX_WIDTH = 1872
MAX_HEIGHT = 1600


class CaptureError(RuntimeError):
    """A capture protocol or framebuffer validation failure."""


def parse_header(line: bytes) -> tuple[int, int, int]:
    if line.startswith(ERROR_PREFIX):
        reason = line[len(ERROR_PREFIX) :].strip().decode("ascii", "replace")
        raise CaptureError(f"device rejected capture: {reason}")
    match = OK_PATTERN.fullmatch(line)
    if match is None:
        raise CaptureError(f"unexpected device response: {line!r}")
    width, height, length = (int(value) for value in match.groups())
    if width <= 0 or height <= 0 or length <= 0:
        raise CaptureError("device reported invalid capture dimensions or length")
    if width > MAX_WIDTH or height > MAX_HEIGHT:
        raise CaptureError(f"device reported unsupported dimensions: {width}x{height}")
    expected_length = BMP_PIXEL_OFFSET + ((width + 3) & ~3) * height
    if length != expected_length:
        raise CaptureError(
            f"device reported invalid BMP length: {length} != {expected_length}"
        )
    return width, height, length


def validate_bmp(payload: bytes, expected_width: int, expected_height: int) -> None:
    if len(payload) < 54 or payload[:2] != b"BM":
        raise CaptureError("payload is not a BMP")
    file_size = struct.unpack_from("<I", payload, 2)[0]
    width, height = struct.unpack_from("<ii", payload, 18)
    planes, bits_per_pixel = struct.unpack_from("<HH", payload, 26)
    compression = struct.unpack_from("<I", payload, 30)[0]
    if file_size != len(payload):
        raise CaptureError(
            f"BMP length mismatch: header says {file_size}, received {len(payload)}"
        )
    if (width, height) != (expected_width, expected_height):
        raise CaptureError(
            "BMP dimensions do not match response: "
            f"{width}x{height} != {expected_width}x{expected_height}"
        )
    if planes != 1 or bits_per_pixel != 8 or compression != 0:
        raise CaptureError("device returned an unsupported BMP encoding")


def read_exact(port, length: int, timeout: float) -> bytes:
    deadline = time.monotonic() + timeout
    data = bytearray()
    while len(data) < length:
        if time.monotonic() >= deadline:
            raise CaptureError(
                f"timed out after receiving {len(data)} of {length} bytes"
            )
        chunk = port.read(min(65536, length - len(data)))
        if chunk:
            data.extend(chunk)
            continue
    return bytes(data)


def read_line(port, timeout: float, maximum_length: int = 256) -> bytes:
    deadline = time.monotonic() + timeout
    line = bytearray()
    while len(line) < maximum_length:
        if time.monotonic() >= deadline:
            raise CaptureError("timed out waiting for the device response")
        byte = port.read(1)
        if byte:
            line.extend(byte)
            if byte == b"\n":
                return bytes(line)
    raise CaptureError("device response line is too long")


def wait_for_header(port, timeout: float) -> tuple[int, int, int]:
    deadline = time.monotonic() + timeout
    next_request_at = 0.0
    line = bytearray()
    while time.monotonic() < deadline:
        now = time.monotonic()
        if now >= next_request_at:
            port.write(REQUEST)
            port.flush()
            next_request_at = now + 0.1

        byte = port.read(1)
        if not byte:
            continue
        line.extend(byte)
        if len(line) > 512:
            line.clear()
            continue
        if byte != b"\n":
            continue

        response = bytes(line)
        line.clear()
        if response.startswith(PROTOCOL.encode("ascii")):
            return parse_header(response)
    raise CaptureError("timed out waiting for the device response")


def capture(port_name: str, output: Path, timeout: float) -> tuple[int, int, int]:
    try:
        import serial
    except ImportError as error:
        raise CaptureError(
            "pyserial is required; install it with: python -m pip install pyserial"
        ) from error

    try:
        port = serial.Serial()
        port.port = port_name
        port.baudrate = 115200
        port.timeout = 0.1
        port.write_timeout = 5
        port.dtr = False
        port.rts = False
        with port:
            port.reset_input_buffer()
            width, height, length = wait_for_header(port, timeout)
            payload = read_exact(port, length, timeout)
            trailer = read_line(port, timeout)
    except CaptureError:
        raise
    except serial.SerialException as error:
        raise CaptureError(f"could not use {port_name}: {error}") from error

    match = END_PATTERN.fullmatch(trailer)
    if match is None:
        raise CaptureError(f"invalid capture trailer: {trailer!r}")
    expected_crc = int(match.group(1), 16)
    actual_crc = binascii.crc32(payload) & 0xFFFFFFFF
    if actual_crc != expected_crc:
        raise CaptureError(
            f"CRC32 mismatch: device sent {expected_crc:08X}, "
            f"received {actual_crc:08X}"
        )
    validate_bmp(payload, width, height)

    temporary = output.with_name(f"{output.name}.part")
    try:
        temporary.write_bytes(payload)
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)
    return width, height, length


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "port", help="USB serial port, for example COM8 or /dev/ttyUSB0"
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=None,
        help="output BMP path (default: screenshot-<unix-epoch>.bmp)",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=360.0,
        help="seconds allowed for each response phase (default: 360)",
    )
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    output = args.output or Path(f"screenshot-{int(time.time())}.bmp")
    try:
        width, height, length = capture(args.port, output, args.timeout)
    except (CaptureError, OSError) as error:
        print(f"capture failed: {error}", file=sys.stderr)
        print(
            "Wake or reset the sleeping device while USB is connected, then retry.",
            file=sys.stderr,
        )
        return 1

    print(f"saved {output} ({width}x{height}, {length} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
