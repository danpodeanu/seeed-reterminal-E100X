#!/usr/bin/env python3
"""Retrieve the current reTerminal framebuffer as an indexed PNG over USB CDC."""

from __future__ import annotations

import argparse
import binascii
import os
import re
import struct
import sys
import tempfile
import time
import zlib
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
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
PNG_PALETTE_BYTES = 256 * 3
PNG_FIXED_BYTES = 837
MAX_WIDTH = 1872
MAX_HEIGHT = 1600


class CaptureError(RuntimeError):
    """A capture protocol or framebuffer validation failure."""


def png_payload_length(width: int, height: int) -> int:
    row_bytes = width + 1
    idat_data_bytes = 2 + (5 + row_bytes) * height + 4
    return PNG_FIXED_BYTES + idat_data_bytes


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
    expected_length = png_payload_length(width, height)
    if length != expected_length:
        raise CaptureError(
            f"device reported invalid PNG length: {length} != {expected_length}"
        )
    return width, height, length


def _parse_png_chunks(payload: bytes) -> list[tuple[bytes, bytes]]:
    if not payload.startswith(PNG_SIGNATURE):
        raise CaptureError("payload is not a PNG")

    chunks = []
    offset = len(PNG_SIGNATURE)
    while offset < len(payload):
        if len(payload) - offset < 12:
            raise CaptureError("PNG contains a truncated chunk")
        length = struct.unpack_from(">I", payload, offset)[0]
        chunk_end = offset + 12 + length
        if chunk_end > len(payload):
            raise CaptureError("PNG chunk length exceeds the payload")
        chunk_type = payload[offset + 4 : offset + 8]
        data = payload[offset + 8 : offset + 8 + length]
        expected_crc = struct.unpack_from(">I", payload, offset + 8 + length)[0]
        actual_crc = binascii.crc32(chunk_type + data) & 0xFFFFFFFF
        if actual_crc != expected_crc:
            name = chunk_type.decode("ascii", "replace")
            raise CaptureError(f"PNG {name} chunk CRC32 mismatch")
        chunks.append((chunk_type, data))
        offset = chunk_end
        if chunk_type == b"IEND":
            break

    if offset != len(payload):
        raise CaptureError("PNG has trailing bytes after IEND")
    return chunks


def _validate_idat(data: bytes, width: int, height: int) -> None:
    if data[:2] != b"\x78\x01":
        raise CaptureError("PNG IDAT does not use the expected zlib encoding")

    offset = 2
    adler = 1
    for y in range(height):
        if len(data) - offset < 5:
            raise CaptureError("PNG IDAT contains a truncated DEFLATE block")
        expected_header = 1 if y + 1 == height else 0
        if data[offset] != expected_header:
            raise CaptureError("PNG IDAT has an invalid stored-block header")
        length, inverse_length = struct.unpack_from("<HH", data, offset + 1)
        if length != width + 1 or inverse_length != (length ^ 0xFFFF):
            raise CaptureError("PNG IDAT has an invalid stored-block length")
        offset += 5
        block_end = offset + length
        if block_end > len(data):
            raise CaptureError("PNG IDAT contains truncated scanline data")
        scanline = data[offset:block_end]
        if scanline[0] != 0:
            raise CaptureError("PNG IDAT uses an unsupported scanline filter")
        adler = zlib.adler32(scanline, adler)
        offset = block_end

    if len(data) - offset != 4:
        raise CaptureError("PNG IDAT has an invalid zlib trailer length")
    expected_adler = struct.unpack_from(">I", data, offset)[0]
    if (adler & 0xFFFFFFFF) != expected_adler:
        raise CaptureError("PNG IDAT Adler-32 mismatch")


def validate_png(payload: bytes, expected_width: int, expected_height: int) -> None:
    chunks = _parse_png_chunks(payload)
    if [chunk_type for chunk_type, _ in chunks] != [
        b"IHDR",
        b"PLTE",
        b"IDAT",
        b"IEND",
    ]:
        raise CaptureError("PNG does not contain the expected chunk sequence")

    ihdr = chunks[0][1]
    if len(ihdr) != 13:
        raise CaptureError("PNG IHDR has an invalid length")
    width, height, bit_depth, color_type, compression, filter_method, interlace = (
        struct.unpack(">IIBBBBB", ihdr)
    )
    if (width, height) != (expected_width, expected_height):
        raise CaptureError(
            "PNG dimensions do not match response: "
            f"{width}x{height} != {expected_width}x{expected_height}"
        )
    expected_length = png_payload_length(expected_width, expected_height)
    if len(payload) != expected_length:
        raise CaptureError(
            f"PNG length mismatch: received {len(payload)}, expected {expected_length}"
        )
    if (
        bit_depth != 8
        or color_type != 3
        or compression != 0
        or filter_method != 0
        or interlace != 0
    ):
        raise CaptureError("device returned an unsupported PNG encoding")
    if len(chunks[1][1]) != PNG_PALETTE_BYTES:
        raise CaptureError("PNG PLTE does not contain 256 RGB entries")
    if len(chunks[2][1]) != 2 + (width + 6) * height + 4:
        raise CaptureError("PNG IDAT length does not match the response dimensions")
    if chunks[3][1]:
        raise CaptureError("PNG IEND chunk is not empty")
    _validate_idat(chunks[2][1], width, height)


def validate_stream_crc(payload: bytes, trailer: bytes) -> None:
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


def save_atomically(output: Path, payload: bytes) -> None:
    temporary_path = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb",
            dir=output.parent,
            prefix=f".{output.name}.",
            suffix=".part",
            delete=False,
        ) as temporary:
            temporary_path = Path(temporary.name)
            temporary.write(payload)
            temporary.flush()
            os.fsync(temporary.fileno())
        os.replace(temporary_path, output)
        temporary_path = None
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)


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

    validate_stream_crc(payload, trailer)
    validate_png(payload, width, height)
    save_atomically(output, payload)
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
        help="output PNG path (default: screenshot-<unix-epoch>.png)",
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

    output = args.output or Path(f"screenshot-{int(time.time())}.png")
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
