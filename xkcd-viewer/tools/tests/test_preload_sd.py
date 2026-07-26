import json
import sys
import tempfile
import threading
import unittest
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIR))

import preload_sd  # noqa: E402


def _process(number, cache_dir, *, force=False, comics=None, skipped=None):
    """Test helper that fills in the manifest-snapshot plumbing."""
    if comics is None:
        comics = {}
    if skipped is None:
        skipped = frozenset()
    return preload_sd.process_comic(
        number,
        cache_dir,
        timeout=0.01,
        retries=1,
        force=force,
        stop_event=threading.Event(),
        manifest_snapshot=comics,
        skipped_snapshot=skipped,
    )


def _png_bytes():
    return b"\x89PNG\r\n\x1a\nvalid"


def _jpg_bytes():
    return b"\xff\xd8\xff\xdbcached"


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
            (cache_dir / "1.jpg").write_bytes(_jpg_bytes())
            snapshot = {
                1: preload_sd.ComicMeta(
                    title="Barrel",
                    alt="alt",
                    extension=".jpg",
                    url="https://imgs.xkcd.com/comics/barrel_cropped_(1).jpg",
                )
            }

            result = _process(1, cache_dir, comics=snapshot)

            self.assertEqual(result.status, "cached")
            self.assertIsNotNone(result.meta)
            self.assertEqual(result.meta.extension, ".jpg")

    def test_missing_comic_404_is_skipped(self):
        with tempfile.TemporaryDirectory() as temporary:
            result = _process(404, Path(temporary))

            self.assertEqual(result.status, "skipped")

    def test_persisted_skip_short_circuits_before_network(self):
        with tempfile.TemporaryDirectory() as temporary:
            cache_dir = Path(temporary)
            skipped = frozenset({5})

            result = _process(5, cache_dir, skipped=skipped)

            self.assertEqual(result.status, "skipped")
            self.assertEqual(result.detail, "previously marked non-retriable")

    def test_manifest_encoding_is_stable_and_sorted(self):
        manifest = preload_sd.Manifest(
            latest=3,
            comics={
                3: preload_sd.ComicMeta(
                    title="Three", alt="alt3", extension=".png",
                    url="https://imgs.xkcd.com/comics/three.png",
                ),
                1: preload_sd.ComicMeta(
                    title="One", alt="alt1", extension=".jpg",
                    url="https://imgs.xkcd.com/comics/one.jpg",
                ),
            },
            skipped={10, 2, 2},
        )

        encoded = preload_sd.encode_manifest(manifest)
        decoded = json.loads(encoded.decode("utf-8"))

        self.assertEqual(decoded["version"], 4)
        self.assertEqual(decoded["latest"], 3)
        self.assertEqual(decoded["skipped"], [2, 10])
        # Comic keys are stringified numbers and should preserve every field.
        self.assertEqual(list(decoded["comics"].keys()), ["1", "3"])
        self.assertEqual(
            decoded["comics"]["1"],
            {"t": "One", "a": "alt1", "e": ".jpg",
             "u": "https://imgs.xkcd.com/comics/one.jpg"},
        )

    def test_manifest_round_trips_through_disk(self):
        with tempfile.TemporaryDirectory() as temporary:
            cache_dir = Path(temporary)
            manifest = preload_sd.Manifest(
                latest=8,
                comics={
                    8: preload_sd.ComicMeta(
                        title="Eight", alt="", extension=".png",
                        url="https://imgs.xkcd.com/comics/eight.png",
                    ),
                },
                skipped={11, 42},
            )

            preload_sd.write_manifest(cache_dir, manifest)

            reloaded = preload_sd.load_manifest(cache_dir)
            self.assertEqual(reloaded.latest, 8)
            self.assertEqual(set(reloaded.comics), {8})
            self.assertEqual(reloaded.comics[8].url,
                             "https://imgs.xkcd.com/comics/eight.png")
            self.assertEqual(reloaded.skipped, {11, 42})

    def test_legacy_per_comic_json_and_latest_are_migrated(self):
        with tempfile.TemporaryDirectory() as temporary:
            cache_dir = Path(temporary)
            complete = {
                "num": 2,
                "img": "https://imgs.xkcd.com/comics/example.png",
                "safe_title": "Example",
                "alt": "alt text",
            }
            (cache_dir / "2.json").write_text(json.dumps(complete))
            (cache_dir / "2.png").write_bytes(_png_bytes())
            # A file whose image is missing must NOT be migrated.
            (cache_dir / "3.json").write_text(
                json.dumps({"num": 3, "img": "https://imgs.xkcd.com/comics/missing.png"})
            )
            (cache_dir / "latest.json").write_text(json.dumps(complete))

            manifest = preload_sd.load_manifest(cache_dir)

            self.assertEqual(set(manifest.comics), {2})
            self.assertEqual(manifest.comics[2].title, "Example")
            self.assertEqual(manifest.comics[2].extension, ".png")
            # Legacy files are gone.
            self.assertFalse((cache_dir / "2.json").exists())
            self.assertFalse((cache_dir / "3.json").exists())
            self.assertFalse((cache_dir / "latest.json").exists())

    def test_legacy_v1_txt_index_and_skip_markers_migrate_to_json(self):
        with tempfile.TemporaryDirectory() as temporary:
            cache_dir = Path(temporary)
            (cache_dir / preload_sd.CACHE_INDEX_LEGACY_TXT).write_text(
                "XKCD_CACHE_INDEX_V2\n1\n2\n1\n7\n"
            )
            (cache_dir / "5.skip").write_text("")
            (cache_dir / "9.skip").write_text("")

            manifest = preload_sd.load_manifest(cache_dir)

            self.assertEqual(manifest.skipped, {5, 7, 9})
            self.assertFalse((cache_dir / "5.skip").exists())
            self.assertFalse((cache_dir / "9.skip").exists())
            self.assertFalse((cache_dir / preload_sd.CACHE_INDEX_LEGACY_TXT).exists())

    def test_v3_json_index_preserves_skips_and_ingests_per_comic_json(self):
        with tempfile.TemporaryDirectory() as temporary:
            cache_dir = Path(temporary)
            v3_index = {"version": 3, "cached": [2], "skipped": [7, 11]}
            (cache_dir / preload_sd.CACHE_INDEX_NAME).write_text(json.dumps(v3_index))
            (cache_dir / "2.json").write_text(json.dumps({
                "num": 2, "img": "https://imgs.xkcd.com/comics/example.png",
                "safe_title": "Example",
            }))
            (cache_dir / "2.png").write_bytes(_png_bytes())

            manifest = preload_sd.load_manifest(cache_dir)

            self.assertEqual(manifest.skipped, {7, 11})
            self.assertEqual(set(manifest.comics), {2})
            self.assertFalse((cache_dir / "2.json").exists())


if __name__ == "__main__":
    unittest.main()
