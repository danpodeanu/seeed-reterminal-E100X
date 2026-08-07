import importlib.util
import struct
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image


MODULE_PATH = Path(__file__).parents[1] / "prepare_photos.py"
SPEC = importlib.util.spec_from_file_location("prepare_photos", MODULE_PATH)
prepare_photos = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = prepare_photos
SPEC.loader.exec_module(prepare_photos)


class PreparePhotosTests(unittest.TestCase):
    def test_e1005_orientation_dimensions(self):
        portrait = prepare_photos.profile_for("e1005", "portrait")
        clockwise = prepare_photos.profile_for("e1005", "rotate-cw")
        counterclockwise = prepare_photos.profile_for("e1005", "rotate-ccw")
        self.assertEqual((480, 800), (portrait.width, portrait.height))
        self.assertEqual((800, 480), (clockwise.width, clockwise.height))
        self.assertEqual((800, 480), (counterclockwise.width, counterclockwise.height))

    def test_non_e1005_rejects_orientation_override(self):
        with self.assertRaises(ValueError):
            prepare_photos.profile_for("e1001", "rotate-cw")

    def test_bw_bmp_remains_four_bit_interchange_format(self):
        colors = ((0, 0, 0), (255, 255, 255))
        indexed = Image.new("P", (8, 2), 1)
        indexed.putpalette(prepare_photos.fixed_palette_image(colors).getpalette())
        indexed.putpixel((0, 0), 0)
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "bw.bmp"
            prepare_photos.write_4bit_bmp(path, indexed, colors)
            data = path.read_bytes()
        self.assertEqual(b"BM", data[:2])
        self.assertEqual((8, 2), struct.unpack_from("<ii", data, 18))
        self.assertEqual(4, struct.unpack_from("<H", data, 28)[0])
        self.assertEqual(2, struct.unpack_from("<I", data, 50)[0])


if __name__ == "__main__":
    unittest.main()
