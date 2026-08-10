import binascii
import struct
import tempfile
import time
import unittest
import zlib

from tools import capture_screen


def png_chunk(chunk_type: bytes, data: bytes) -> bytes:
    crc = binascii.crc32(chunk_type + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + chunk_type + data + struct.pack(">I", crc)


def indexed_png(width: int, height: int) -> bytes:
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0)
    palette = bytes(value for index in range(256) for value in (index, index, index))
    idat = bytearray(b"\x78\x01")
    adler = 1
    for y in range(height):
        scanline = b"\0" + bytes((x + y) & 0xFF for x in range(width))
        length = len(scanline)
        idat.extend(
            struct.pack("<BHH", 1 if y + 1 == height else 0, length, length ^ 0xFFFF)
        )
        idat.extend(scanline)
        adler = zlib.adler32(scanline, adler)
    idat.extend(struct.pack(">I", adler & 0xFFFFFFFF))
    return (
        capture_screen.PNG_SIGNATURE
        + png_chunk(b"IHDR", ihdr)
        + png_chunk(b"PLTE", palette)
        + png_chunk(b"IDAT", bytes(idat))
        + png_chunk(b"IEND", b"")
    )


class CaptureScreenTest(unittest.TestCase):
    def test_png_payload_length_matches_streamed_layout(self):
        self.assertEqual(capture_screen.png_payload_length(5, 2), 865)
        self.assertEqual(capture_screen.png_payload_length(480, 800), 389643)
        self.assertEqual(capture_screen.png_payload_length(1872, 1404), 2637555)

    def test_parse_header(self):
        self.assertEqual(
            capture_screen.parse_header(
                b"RETERMINAL_SCREEN_CAPTURE_V1 OK 480 800 389643\n"
            ),
            (480, 800, 389643),
        )

    def test_parse_error_response(self):
        with self.assertRaisesRegex(
            capture_screen.CaptureError, "framebuffer-unavailable"
        ):
            capture_screen.parse_header(
                b"RETERMINAL_SCREEN_CAPTURE_V1 ERROR framebuffer-unavailable\n"
            )

    def test_parse_header_rejects_inconsistent_payload_length(self):
        with self.assertRaisesRegex(capture_screen.CaptureError, "PNG length"):
            capture_screen.parse_header(
                b"RETERMINAL_SCREEN_CAPTURE_V1 OK 480 800 999999999\n"
            )

    def test_validate_png_accepts_matching_indexed_image(self):
        payload = indexed_png(5, 2)
        self.assertEqual(len(payload), capture_screen.png_payload_length(5, 2))
        capture_screen.validate_png(payload, 5, 2)

    def test_validate_png_rejects_bad_signature(self):
        payload = bytearray(indexed_png(5, 2))
        payload[0] = 0
        with self.assertRaisesRegex(capture_screen.CaptureError, "not a PNG"):
            capture_screen.validate_png(bytes(payload), 5, 2)

    def test_validate_png_rejects_mismatched_dimensions(self):
        with self.assertRaisesRegex(capture_screen.CaptureError, "dimensions"):
            capture_screen.validate_png(indexed_png(5, 2), 4, 2)

    def test_validate_png_rejects_unsupported_encoding(self):
        payload = bytearray(indexed_png(5, 2))
        payload[24] = 2
        ihdr_crc = binascii.crc32(payload[12:29]) & 0xFFFFFFFF
        struct.pack_into(">I", payload, 29, ihdr_crc)
        with self.assertRaisesRegex(capture_screen.CaptureError, "encoding"):
            capture_screen.validate_png(bytes(payload), 5, 2)

    def test_validate_png_rejects_chunk_crc_mismatch(self):
        payload = bytearray(indexed_png(5, 2))
        payload[40] ^= 1
        with self.assertRaisesRegex(capture_screen.CaptureError, "chunk CRC32"):
            capture_screen.validate_png(bytes(payload), 5, 2)

    def test_validate_png_rejects_malformed_deflate_block(self):
        payload = bytearray(indexed_png(5, 2))
        idat_data_offset = 8 + 25 + 780 + 8
        payload[idat_data_offset + 2] = 1
        idat_length = struct.unpack_from(">I", payload, idat_data_offset - 8)[0]
        idat_type_offset = idat_data_offset - 4
        crc = binascii.crc32(
            payload[idat_type_offset : idat_data_offset + idat_length]
        ) & 0xFFFFFFFF
        struct.pack_into(">I", payload, idat_data_offset + idat_length, crc)
        with self.assertRaisesRegex(capture_screen.CaptureError, "block header"):
            capture_screen.validate_png(bytes(payload), 5, 2)

    def test_validate_png_rejects_adler_mismatch(self):
        payload = bytearray(indexed_png(5, 2))
        idat_data_offset = 8 + 25 + 780 + 8
        idat_length = struct.unpack_from(">I", payload, idat_data_offset - 8)[0]
        payload[idat_data_offset + idat_length - 1] ^= 1
        idat_type_offset = idat_data_offset - 4
        crc = binascii.crc32(
            payload[idat_type_offset : idat_data_offset + idat_length]
        ) & 0xFFFFFFFF
        struct.pack_into(">I", payload, idat_data_offset + idat_length, crc)
        with self.assertRaisesRegex(capture_screen.CaptureError, "Adler-32"):
            capture_screen.validate_png(bytes(payload), 5, 2)

    def test_stream_crc_accepts_standard_vector(self):
        capture_screen.validate_stream_crc(
            b"123456789", b"RETERMINAL_SCREEN_CAPTURE_V1 END CBF43926\n"
        )

    def test_stream_crc_rejects_corruption(self):
        with self.assertRaisesRegex(capture_screen.CaptureError, "CRC32 mismatch"):
            capture_screen.validate_stream_crc(
                b"123456788", b"RETERMINAL_SCREEN_CAPTURE_V1 END CBF43926\n"
            )

    def test_save_atomically_replaces_destination_without_part_file(self):
        with tempfile.TemporaryDirectory() as directory:
            output = capture_screen.Path(directory) / "capture.png"
            output.write_bytes(b"old")
            capture_screen.save_atomically(output, b"new")
            self.assertEqual(output.read_bytes(), b"new")
            self.assertEqual(list(output.parent.glob("*.part")), [])

    def test_wait_for_header_ignores_logs_and_sends_request(self):
        class FakePort:
            def __init__(self):
                self.received = bytearray(
                    b"[games] entering light sleep\n"
                    b"RETERMINAL_SCREEN_CAPTURE_V1 OK 480 800 389643\n"
                )
                self.sent = bytearray()

            def read(self, _length):
                if not self.received:
                    return b""
                byte = bytes(self.received[:1])
                del self.received[:1]
                return byte

            def write(self, data):
                self.sent.extend(data)
                return len(data)

            def flush(self):
                pass

        port = FakePort()
        self.assertEqual(
            capture_screen.wait_for_header(port, 1), (480, 800, 389643)
        )
        self.assertEqual(port.sent, capture_screen.REQUEST)

    def test_read_exact_enforces_deadline_while_bytes_are_trickling(self):
        class SlowPort:
            def read(self, _length):
                time.sleep(0.02)
                return b"x"

        with self.assertRaisesRegex(capture_screen.CaptureError, "timed out"):
            capture_screen.read_exact(SlowPort(), 5, 0.03)


if __name__ == "__main__":
    unittest.main()
