import json
import sys
import tempfile
import threading
import unittest
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIR))

import preload_sd  # noqa: E402


class PreloadSdTests(unittest.TestCase):
    def test_metadata_validation(self):
        raw = json.dumps(
            {
                "num": 42,
                "img": "https://imgs.xkcd.com/comics/geico.png",
                "safe_title": "Geico",
            }
        ).encode()

        metadata = preload_sd.decode_metadata(raw, 42)

        self.assertEqual(metadata["num"], 42)
        with self.assertRaises(ValueError):
            preload_sd.decode_metadata(raw, 41)

    def test_supported_image_extensions_are_normalized(self):
        self.assertEqual(
            preload_sd.image_extension(
                "https://imgs.xkcd.com/comics/example.PNG?cache=1"
            ),
            ".png",
        )
        self.assertEqual(
            preload_sd.image_extension("https://example.com/image.webp"), ""
        )

    def test_existing_complete_entry_is_resumable_without_network(self):
        with tempfile.TemporaryDirectory() as temporary:
            cache_dir = Path(temporary)
            metadata = {
                "num": 1,
                "img": "https://imgs.xkcd.com/comics/barrel_cropped_(1).jpg",
            }
            (cache_dir / "1.json").write_text(json.dumps(metadata))
            (cache_dir / "1.jpg").write_bytes(b"\xff\xd8\xff\xdbcached")

            result = preload_sd.process_comic(
                1,
                cache_dir,
                timeout=0.01,
                retries=1,
                force=False,
                stop_event=threading.Event(),
            )

            self.assertEqual(result.status, "cached")

    def test_missing_comic_404_is_skipped(self):
        with tempfile.TemporaryDirectory() as temporary:
            result = preload_sd.process_comic(
                404,
                Path(temporary),
                timeout=0.01,
                retries=1,
                force=False,
                stop_event=threading.Event(),
            )

            self.assertEqual(result.status, "skipped")

    def test_cache_index_contains_only_complete_valid_comics(self):
        with tempfile.TemporaryDirectory() as temporary:
            cache_dir = Path(temporary)
            complete = {
                "num": 2,
                "img": "https://imgs.xkcd.com/comics/example.png",
            }
            missing_image = {
                "num": 3,
                "img": "https://imgs.xkcd.com/comics/missing.png",
            }
            (cache_dir / "2.json").write_text(json.dumps(complete))
            (cache_dir / "2.png").write_bytes(b"\x89PNG\r\n\x1a\nvalid")
            (cache_dir / "3.json").write_text(json.dumps(missing_image))
            (cache_dir / "404.json").write_text(
                json.dumps({"num": 404, "img": "https://example.com/404.png"})
            )

            numbers = preload_sd.write_cache_index(cache_dir)

            self.assertEqual(numbers, [2])
            self.assertEqual(
                (cache_dir / preload_sd.CACHE_INDEX_NAME).read_text(),
                "XKCD_CACHE_INDEX_V1\n1\n2\n",
            )

    def test_cache_index_encoding_is_sorted_and_unique(self):
        self.assertEqual(
            preload_sd.encode_cache_index([7, 1, 7, 3]),
            b"XKCD_CACHE_INDEX_V1\n3\n1\n3\n7\n",
        )


if __name__ == "__main__":
    unittest.main()
