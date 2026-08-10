import binascii
import struct
import time
import unittest

from tools import capture_screen


class CaptureScreenTest(unittest.TestCase):
    def test_parse_header(self):
        self.assertEqual(
            capture_screen.parse_header(
                b"RETERMINAL_SCREEN_CAPTURE_V1 OK 480 800 385078\n"
            ),
            (480, 800, 385078),
        )

    def test_parse_error_response(self):
        with self.assertRaisesRegex(
            capture_screen.CaptureError, "framebuffer-unavailable"
        ):
            capture_screen.parse_header(
                b"RETERMINAL_SCREEN_CAPTURE_V1 ERROR framebuffer-unavailable\n"
            )

    def test_parse_header_rejects_inconsistent_payload_length(self):
        with self.assertRaisesRegex(capture_screen.CaptureError, "BMP length"):
            capture_screen.parse_header(
                b"RETERMINAL_SCREEN_CAPTURE_V1 OK 480 800 999999999\n"
            )

    def test_validate_bmp_accepts_matching_uncompressed_indexed_image(self):
        payload = bytearray(1094)
        payload[:2] = b"BM"
        struct.pack_into("<I", payload, 2, len(payload))
        struct.pack_into("<I", payload, 10, 1078)
        struct.pack_into("<I", payload, 14, 40)
        struct.pack_into("<ii", payload, 18, 5, 2)
        struct.pack_into("<HH", payload, 26, 1, 8)
        struct.pack_into("<I", payload, 30, 0)
        capture_screen.validate_bmp(bytes(payload), 5, 2)

    def test_python_crc_matches_device_standard_vector(self):
        self.assertEqual(binascii.crc32(b"123456789") & 0xFFFFFFFF, 0xCBF43926)

    def test_wait_for_header_ignores_logs_and_sends_request(self):
        class FakePort:
            def __init__(self):
                self.received = bytearray(
                    b"[games] entering light sleep\n"
                    b"RETERMINAL_SCREEN_CAPTURE_V1 OK 480 800 385078\n"
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
            capture_screen.wait_for_header(port, 1), (480, 800, 385078)
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
