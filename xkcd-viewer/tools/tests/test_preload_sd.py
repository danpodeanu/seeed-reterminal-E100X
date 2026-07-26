import json
import sys
import tempfile
import threading
import unittest
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIR))

import preload_sd  # noqa: E402


def _process(number, cache_dir, *, force=False, skipped=None):
    """Test helper that fills in the skip-set plumbing."""
    if skipped is None:
        skipped = set()
    return preload_sd.process_comic(
        number,
        cache_dir,
        timeout=0.01,
        retries=1,
        force=force,
        stop_event=threading.Event(),
        skipped=skipped,
        skipped_lock=threading.Lock(),
    )


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

            result = _process(1, cache_dir)

            self.assertEqual(result.status, "cached")

    def test_missing_comic_404_is_skipped(self):
        with tempfile.TemporaryDirectory() as temporary:
            result = _process(404, Path(temporary))

            self.assertEqual(result.status, "skipped")

    def test_unsupported_extension_records_and_reuses_skip_verdict(self):
        with tempfile.TemporaryDirectory() as temporary:
            cache_dir = Path(temporary)
            metadata = {
                "num": 5,
                "img": "https://imgs.xkcd.com/comics/example.gif",
            }
            (cache_dir / "5.json").write_text(json.dumps(metadata))
            skipped: set[int] = set()

            first = _process(5, cache_dir, skipped=skipped)
            self.assertEqual(first.status, "skipped")
            self.assertEqual(first.detail, "unsupported image extension")
            self.assertIn(5, skipped)

            # A second run must not touch the network; passing an obviously
            # broken metadata file exercises that path — the persisted skip
            # short-circuits before decode_metadata is ever called.
            (cache_dir / "5.json").write_text("not valid json")

            second = _process(5, cache_dir, skipped=skipped)
            self.assertEqual(second.status, "skipped")
            self.assertEqual(second.detail, "previously marked non-retriable")

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

            cached, skipped = preload_sd.write_cache_index(cache_dir, skipped=set())

            self.assertEqual(cached, [2])
            self.assertEqual(skipped, [])
            self.assertEqual(
                (cache_dir / preload_sd.CACHE_INDEX_NAME).read_text(),
                "XKCD_CACHE_INDEX_V2\n1\n2\n0\n",
            )

    def test_cache_index_encoding_is_sorted_and_unique(self):
        self.assertEqual(
            preload_sd.encode_cache_index([7, 1, 7, 3], [10, 2, 2]),
            b"XKCD_CACHE_INDEX_V2\n3\n1\n3\n7\n2\n2\n10\n",
        )

    def test_persisted_skip_list_round_trips(self):
        with tempfile.TemporaryDirectory() as temporary:
            cache_dir = Path(temporary)
            complete = {
                "num": 8,
                "img": "https://imgs.xkcd.com/comics/example.png",
            }
            (cache_dir / "8.json").write_text(json.dumps(complete))
            (cache_dir / "8.png").write_bytes(b"\x89PNG\r\n\x1a\nvalid")

            preload_sd.write_cache_index(cache_dir, skipped={11, 42})
            self.assertEqual(
                preload_sd.read_cache_index_skips(cache_dir), {11, 42}
            )

            # Rewriting without an explicit skipped argument must
            # preserve the previously persisted set.
            preload_sd.write_cache_index(cache_dir)
            self.assertEqual(
                preload_sd.read_cache_index_skips(cache_dir), {11, 42}
            )

    def test_legacy_v1_index_with_skip_markers_migrates_to_v2(self):
        with tempfile.TemporaryDirectory() as temporary:
            cache_dir = Path(temporary)
            complete = {
                "num": 2,
                "img": "https://imgs.xkcd.com/comics/example.png",
            }
            (cache_dir / "2.json").write_text(json.dumps(complete))
            (cache_dir / "2.png").write_bytes(b"\x89PNG\r\n\x1a\nvalid")
            # A pre-V2 preloader run would have left these behind.
            (cache_dir / preload_sd.CACHE_INDEX_NAME).write_text(
                "XKCD_CACHE_INDEX_V1\n1\n2\n"
            )
            (cache_dir / "5.skip").write_text("")
            (cache_dir / "9.skip").write_text("")

            # Reading the legacy index adopts and consumes the .skip files.
            skips = preload_sd.read_cache_index_skips(cache_dir)
            self.assertEqual(skips, {5, 9})
            self.assertFalse((cache_dir / "5.skip").exists())
            self.assertFalse((cache_dir / "9.skip").exists())

            # Persisting them produces a clean V2 file with both sections.
            preload_sd.write_cache_index(cache_dir, skipped=skips)
            self.assertEqual(
                (cache_dir / preload_sd.CACHE_INDEX_NAME).read_text(),
                "XKCD_CACHE_INDEX_V2\n1\n2\n2\n5\n9\n",
            )


if __name__ == "__main__":
    unittest.main()
