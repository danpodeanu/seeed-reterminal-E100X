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
                    year=2006,
                    month=1,
                    day=1,
                )
            }

            result = _process(1, cache_dir, comics=snapshot)

            self.assertEqual(result.status, "cached")
            self.assertIsNotNone(result.meta)
            self.assertEqual(result.meta.extension, ".jpg")
            # Backfill must not clobber a date that's already present.
            self.assertEqual(result.meta.year, 2006)

    def test_missing_publication_date_triggers_metadata_refetch(self):
        # A v5-manifest ComicMeta has no y/m/d. process_comic must
        # refetch info.0.json so the JSONL entry gains the date, but
        # must NOT redownload the image when it's already on disk.
        fetched_urls: list[str] = []

        def fake_fetch(url, timeout, retries):
            fetched_urls.append(url)
            return json.dumps(
                {
                    "num": 1,
                    "img": "https://imgs.xkcd.com/comics/barrel.jpg",
                    "safe_title": "Barrel",
                    "alt": "alt",
                    "year": "2006",
                    "month": "1",
                    "day": "1",
                }
            ).encode()

        with tempfile.TemporaryDirectory() as temporary:
            cache_dir = Path(temporary)
            (cache_dir / "1.jpg").write_bytes(_jpg_bytes())
            snapshot = {
                1: preload_sd.ComicMeta(
                    title="Barrel",
                    alt="alt",
                    extension=".jpg",
                    url="https://imgs.xkcd.com/comics/barrel.jpg",
                )
            }

            original_fetch = preload_sd.fetch_bytes
            preload_sd.fetch_bytes = fake_fetch
            try:
                result = _process(1, cache_dir, comics=snapshot)
            finally:
                preload_sd.fetch_bytes = original_fetch

            self.assertEqual(result.status, "cached")
            self.assertEqual(result.meta.year, 2006)
            self.assertEqual(result.meta.month, 1)
            self.assertEqual(result.meta.day, 1)
            # Exactly one refetch: the tiny info.0.json, never the image.
            self.assertEqual(len(fetched_urls), 1)
            self.assertIn("info.0.json", fetched_urls[0])

    def test_meta_from_xkcd_json_extracts_publication_date(self):
        # xkcd JSON returns date components sometimes as strings and
        # sometimes as ints; meta_from_xkcd_json must handle both.
        meta = preload_sd.meta_from_xkcd_json(
            {
                "img": "https://imgs.xkcd.com/comics/x.png",
                "safe_title": "X",
                "alt": "a",
                "year": "2021",
                "month": 6,
                "day": "25",
            }
        )
        self.assertEqual((meta.year, meta.month, meta.day), (2021, 6, 25))

    def test_meta_from_xkcd_json_zeroes_out_implausible_dates(self):
        # A missing or garbage date must collapse to 0/0/0 so we never
        # emit "y":"" or a nonsense month into the JSONL manifest.
        meta = preload_sd.meta_from_xkcd_json(
            {
                "img": "https://imgs.xkcd.com/comics/x.png",
                "safe_title": "X",
                "alt": "a",
                "year": "",
                "month": "13",
                "day": "0",
            }
        )
        self.assertEqual((meta.year, meta.month, meta.day), (0, 0, 0))

    def test_manifest_encoding_includes_publication_date_when_set(self):
        manifest = preload_sd.Manifest(
            latest=2,
            comics={
                1: preload_sd.ComicMeta(
                    title="Dated", alt="a", extension=".png",
                    url="https://x/1.png",
                    year=2010, month=3, day=15,
                ),
                2: preload_sd.ComicMeta(
                    title="Undated", alt="b", extension=".png",
                    url="https://x/2.png",
                ),
            },
        )

        lines = preload_sd.encode_manifest(manifest).decode().rstrip("\n").split("\n")
        first = json.loads(lines[1])
        second = json.loads(lines[2])

        self.assertEqual(first["y"], 2010)
        self.assertEqual(first["m"], 3)
        self.assertEqual(first["d"], 15)
        # Undated comics still round-trip without the triplet.
        self.assertNotIn("y", second)
        self.assertNotIn("m", second)
        self.assertNotIn("d", second)

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
        lines = encoded.decode("utf-8").rstrip("\n").split("\n")

        # Header line first, then one line per comic in ascending order.
        header = json.loads(lines[0])
        self.assertEqual(header["v"], 5)
        self.assertEqual(header["l"], 3)
        self.assertEqual(header["s"], [2, 10])

        self.assertEqual(len(lines), 3)
        first = json.loads(lines[1])
        second = json.loads(lines[2])
        self.assertEqual(first["n"], 1)
        self.assertEqual(second["n"], 3)
        self.assertEqual(
            first,
            {"n": 1, "t": "One", "a": "alt1", "e": ".jpg",
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
            # v3/v4 lived at index.json (the legacy filename); v5 uses .jsonl.
            (cache_dir / preload_sd.CACHE_INDEX_LEGACY_JSON).write_text(
                json.dumps(v3_index)
            )
            (cache_dir / "2.json").write_text(json.dumps({
                "num": 2, "img": "https://imgs.xkcd.com/comics/example.png",
                "safe_title": "Example",
            }))
            (cache_dir / "2.png").write_bytes(_png_bytes())

            manifest = preload_sd.load_manifest(cache_dir)

            self.assertEqual(manifest.skipped, {7, 11})
            self.assertEqual(set(manifest.comics), {2})
            self.assertFalse((cache_dir / "2.json").exists())
            self.assertFalse((cache_dir / preload_sd.CACHE_INDEX_LEGACY_JSON).exists())

    def test_v4_json_manifest_upgrades_to_v5_jsonl(self):
        with tempfile.TemporaryDirectory() as temporary:
            cache_dir = Path(temporary)
            v4_document = {
                "version": 4,
                "latest": 8,
                "comics": {
                    "8": {
                        "t": "Eight",
                        "a": "alt8",
                        "e": ".png",
                        "u": "https://imgs.xkcd.com/comics/eight.png",
                    }
                },
                "skipped": [11, 42],
            }
            (cache_dir / "8.png").write_bytes(_png_bytes())
            (cache_dir / preload_sd.CACHE_INDEX_LEGACY_JSON).write_text(
                json.dumps(v4_document)
            )

            manifest = preload_sd.load_manifest(cache_dir)

            self.assertEqual(manifest.latest, 8)
            self.assertEqual(set(manifest.comics), {8})
            self.assertEqual(manifest.comics[8].title, "Eight")
            self.assertEqual(manifest.skipped, {11, 42})
            self.assertFalse((cache_dir / preload_sd.CACHE_INDEX_LEGACY_JSON).exists())

            # Round-trip: write and reload to confirm the JSONL path works.
            preload_sd.write_manifest(cache_dir, manifest)
            self.assertTrue((cache_dir / preload_sd.CACHE_INDEX_NAME).exists())
            reloaded = preload_sd.load_manifest(cache_dir)
            self.assertEqual(reloaded.latest, 8)
            self.assertEqual(set(reloaded.comics), {8})
            self.assertEqual(reloaded.skipped, {11, 42})


if __name__ == "__main__":
    unittest.main()
