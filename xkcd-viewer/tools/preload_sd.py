#!/usr/bin/env python3
"""Pre-populate an SD card with the complete XKCD archive.

The output matches the cache layout used by the XKCD Viewer firmware:

    <SD root>/xkcd/index.jsonl (JSON Lines manifest: header + one line per comic)
    <SD root>/xkcd/1.png
    <SD root>/xkcd/2.png
    ...

All per-comic metadata (title, alt, extension, image URL) lives in the
single manifest — the firmware never needs to open a per-comic .json file
during the hot pick path. JSON Lines means the firmware can parse one
tiny doc per line instead of holding a giant parse tree in memory.
Legacy layouts (per-comic `.json`, `latest.json`, pre-JSON `index.txt`,
v4 single-doc `index.json`, stray `.skip` markers) are migrated on first
run and their files are deleted.

The command is resumable. Images already on the card whose manifest entry
matches xkcd's current metadata are not downloaded again unless --force
is used.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import errno
import json
import os
import shutil
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable


LATEST_URL = "https://xkcd.com/info.0.json"
COMIC_METADATA_URL = "https://xkcd.com/{number}/info.0.json"
SUPPORTED_EXTENSIONS = {".png", ".jpg", ".jpeg", ".bmp"}
USER_AGENT = (
    "seeed-reterminal-E100X-xkcd-preloader/1.0 "
    "(https://github.com/danpodeanu/seeed-reterminal-E100X)"
)
CHUNK_SIZE = 128 * 1024
CACHE_INDEX_NAME = "index.jsonl"
CACHE_INDEX_LEGACY_JSON = "index.json"
CACHE_INDEX_LEGACY_TXT = "index.txt"
CACHE_INDEX_LEGACY_MAGIC = "XKCD_CACHE_INDEX_V2"
CACHE_INDEX_LEGACY_LATEST = "latest.json"
CACHE_INDEX_VERSION = 5

# Smooth-font sizes the firmware looks for at /fonts/xkcd_<size>.vlw.
# Must stay in sync with SMOOTH_FONT_TITLE_PX / SMOOTH_FONT_FOOTER_PX in main.cpp.
FONT_SIZES_PX = (13, 17, 25, 33)
DEFAULT_TTF = Path(__file__).parent / "fonts" / "DejaVuSans-Bold.ttf"


class DownloadError(RuntimeError):
    """A download failed after all permitted attempts."""


@dataclass(frozen=True)
class ComicMeta:
    title: str
    alt: str
    extension: str
    url: str


@dataclass
class Result:
    number: int
    status: str
    detail: str
    meta: ComicMeta | None = None


@dataclass
class Manifest:
    """In-memory representation of the v5 JSONL manifest."""

    version: int = CACHE_INDEX_VERSION
    latest: int = 0
    comics: dict[int, ComicMeta] = field(default_factory=dict)
    skipped: set[int] = field(default_factory=set)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Download the complete XKCD archive into the cache layout expected "
            "by the reTerminal XKCD Viewer."
        )
    )
    parser.add_argument(
        "sd_root",
        type=Path,
        help="mounted SD-card root, for example /Volumes/XKCD or /media/user/XKCD",
    )
    parser.add_argument(
        "--workers",
        type=int,
        default=4,
        help="parallel downloads (default: 4)",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=30.0,
        help="timeout for each HTTP operation in seconds (default: 30)",
    )
    parser.add_argument(
        "--retries",
        type=int,
        default=3,
        help="attempts per HTTP request (default: 3)",
    )
    parser.add_argument(
        "--start",
        type=int,
        default=1,
        help="first comic number to download (default: 1)",
    )
    parser.add_argument(
        "--end",
        type=int,
        help="last comic number to download (default: latest)",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="redownload files that already pass validation",
    )
    parser.add_argument(
        "--with-fonts",
        action="store_true",
        help=(
            "also generate /fonts/xkcd_<size>.vlw smooth-font files from "
            "tools/fonts/DejaVuSans-Bold.ttf. Required for on-device UTF-8 "
            "rendering; safe to omit if the fonts are already on the card."
        ),
    )
    parser.add_argument(
        "--fonts-ttf",
        type=Path,
        default=DEFAULT_TTF,
        help=(
            "TTF/OTF source for --with-fonts (default: "
            "tools/fonts/DejaVuSans-Bold.ttf)"
        ),
    )
    args = parser.parse_args()

    if args.workers < 1 or args.workers > 32:
        parser.error("--workers must be between 1 and 32")
    if args.timeout <= 0:
        parser.error("--timeout must be greater than zero")
    if args.retries < 1:
        parser.error("--retries must be at least 1")
    if args.start < 1:
        parser.error("--start must be at least 1")
    if args.end is not None and args.end < args.start:
        parser.error("--end must not be less than --start")
    return args


def request(url: str, timeout: float):
    headers = {
        "User-Agent": USER_AGENT,
        "Accept": "application/json,image/*,*/*;q=0.8",
    }
    return urllib.request.urlopen(
        urllib.request.Request(url, headers=headers), timeout=timeout
    )


def retry_delay(attempt: int) -> float:
    return min(2 ** (attempt - 1), 8)


def fetch_bytes(url: str, timeout: float, retries: int) -> bytes:
    last_error: Exception | None = None
    for attempt in range(1, retries + 1):
        try:
            with request(url, timeout) as response:
                return response.read()
        except urllib.error.HTTPError as exc:
            last_error = exc
            if exc.code not in {408, 425, 429, 500, 502, 503, 504}:
                break
        except (urllib.error.URLError, TimeoutError, OSError) as exc:
            last_error = exc
        if attempt < retries:
            time.sleep(retry_delay(attempt))
    raise DownloadError(f"{url}: {last_error}") from last_error


def decode_metadata(raw: bytes, expected_number: int | None = None) -> dict[str, Any]:
    try:
        metadata = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"invalid JSON: {exc}") from exc

    number = metadata.get("num")
    image_url = metadata.get("img")
    if not isinstance(number, int) or number < 1:
        raise ValueError("metadata has no valid comic number")
    if expected_number is not None and number != expected_number:
        raise ValueError(
            f"metadata returned comic #{number}, expected #{expected_number}"
        )
    if not isinstance(image_url, str) or not image_url:
        raise ValueError("metadata has no image URL")
    parsed = urllib.parse.urlsplit(image_url)
    if parsed.scheme not in {"http", "https"} or not parsed.netloc:
        raise ValueError("metadata contains an invalid image URL")
    return metadata


def meta_from_xkcd_json(raw: dict[str, Any]) -> ComicMeta | None:
    """Extract a ComicMeta from a decoded xkcd info JSON. Returns None if
    the image URL uses an unsupported extension."""
    image_url = raw["img"]
    extension = image_extension(image_url)
    if not extension:
        return None
    title = raw.get("safe_title") or raw.get("title") or ""
    alt = raw.get("alt") or ""
    return ComicMeta(
        title=str(title),
        alt=str(alt),
        extension=extension,
        url=str(image_url),
    )


def atomic_write(path: Path, data: bytes) -> None:
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise


def image_extension(image_url: str) -> str:
    extension = Path(urllib.parse.urlsplit(image_url).path).suffix.lower()
    return extension if extension in SUPPORTED_EXTENSIONS else ""


def valid_image_header(path: Path, extension: str) -> bool:
    try:
        if path.stat().st_size == 0:
            return False
        with path.open("rb") as image:
            header = image.read(12)
    except OSError:
        return False

    if extension == ".png":
        return header.startswith(b"\x89PNG\r\n\x1a\n")
    if extension in {".jpg", ".jpeg"}:
        return header.startswith(b"\xff\xd8\xff")
    if extension == ".bmp":
        return header.startswith(b"BM")
    return False


def encode_manifest(manifest: Manifest) -> bytes:
    if manifest.latest < 0:
        raise ValueError("manifest has an invalid latest number")
    if any(n <= 0 or n == 404 for n in manifest.comics):
        raise ValueError("manifest contains an invalid comic number")
    if any(n <= 0 or n == 404 for n in manifest.skipped):
        raise ValueError("skip list contains an invalid comic number")
    if any(m.extension not in SUPPORTED_EXTENSIONS for m in manifest.comics.values()):
        raise ValueError("manifest contains an unsupported image extension")

    # JSONL: one JSON object per line. Line 1 is the header (version,
    # latest, skips); every subsequent line is one comic keyed by "n".
    # The firmware parses one line at a time with a tiny stack doc, so
    # neither side ever has to hold the whole thing as a parse tree.
    lines: list[str] = []
    header: dict[str, Any] = {
        "v": CACHE_INDEX_VERSION,
        "l": manifest.latest,
        "s": sorted(manifest.skipped),
    }
    lines.append(json.dumps(header, separators=(",", ":"), ensure_ascii=False))
    for number in sorted(manifest.comics):
        m = manifest.comics[number]
        entry: dict[str, Any] = {
            "n": number,
            "t": m.title,
            "a": m.alt,
            "e": m.extension,
            "u": m.url,
        }
        lines.append(json.dumps(entry, separators=(",", ":"), ensure_ascii=False))
    # Trailing newline so tools like `tail` see the final entry.
    return ("\n".join(lines) + "\n").encode("utf-8")


def _adopt_legacy_skip_markers(cache_dir: Path) -> set[int]:
    """Convert any leftover `<n>.skip` sentinel files into a skip set."""
    adopted: set[int] = set()
    for marker in cache_dir.glob("*.skip"):
        try:
            number = int(marker.stem)
        except ValueError:
            continue
        if number > 0 and number != 404:
            adopted.add(number)
        marker.unlink(missing_ok=True)
    return adopted


def _read_legacy_txt_index(path: Path) -> set[int]:
    """Parse the pre-JSON V2 text index. Returns the persisted skip set,
    or empty if the file is missing/unreadable."""
    if not path.exists():
        return set()
    try:
        lines = path.read_text().splitlines()
    except OSError:
        return set()
    if not lines or lines[0].strip() != CACHE_INDEX_LEGACY_MAGIC:
        return set()
    try:
        cached_count = int(lines[1].strip())
        skip_header_index = 2 + cached_count
        skip_count = int(lines[skip_header_index].strip())
    except (IndexError, ValueError):
        return set()
    skipped: set[int] = set()
    for offset in range(skip_count):
        try:
            skipped.add(int(lines[skip_header_index + 1 + offset].strip()))
        except (IndexError, ValueError):
            return set()
    return skipped


def _ingest_legacy_per_comic_json(cache_dir: Path, manifest: Manifest) -> int:
    """Absorb per-comic <n>.json files into the manifest, then delete them.

    Returns the number of files migrated. Called on first v5 run so
    existing SD cards upgrade cleanly. Skips the retired v4 single-doc
    index (`index.json`) and the retired latest.json.
    """
    migrated = 0
    for metadata_path in cache_dir.glob("*.json"):
        if metadata_path.name in {CACHE_INDEX_LEGACY_JSON, CACHE_INDEX_LEGACY_LATEST}:
            continue
        try:
            number = int(metadata_path.stem)
        except ValueError:
            continue
        if number <= 0 or number == 404:
            metadata_path.unlink(missing_ok=True)
            continue
        try:
            raw = decode_metadata(metadata_path.read_bytes(), number)
        except (OSError, ValueError):
            metadata_path.unlink(missing_ok=True)
            continue
        meta = meta_from_xkcd_json(raw)
        if meta is not None and number not in manifest.comics:
            image_path = cache_dir / f"{number}{meta.extension}"
            if valid_image_header(image_path, meta.extension):
                manifest.comics[number] = meta
                migrated += 1
        metadata_path.unlink(missing_ok=True)
    # latest.json is now dead too.
    (cache_dir / CACHE_INDEX_LEGACY_LATEST).unlink(missing_ok=True)
    return migrated


def _ingest_comic_dict(value: Any, manifest: Manifest, number: int) -> None:
    """Add a comic to the manifest if the dict has valid ext + url fields."""
    if not isinstance(value, dict):
        return
    ext = value.get("e", "")
    url = value.get("u", "")
    if not isinstance(ext, str) or ext not in SUPPORTED_EXTENSIONS:
        return
    if not isinstance(url, str) or not url:
        return
    manifest.comics[number] = ComicMeta(
        title=str(value.get("t", "")),
        alt=str(value.get("a", "")),
        extension=ext,
        url=url,
    )


def _ingest_skipped_list(raw: Any, manifest: Manifest) -> None:
    if not isinstance(raw, list):
        return
    for value in raw:
        if (
            isinstance(value, int)
            and not isinstance(value, bool)
            and value > 0
            and value != 404
        ):
            manifest.skipped.add(value)


def _load_v4_json(path: Path, manifest: Manifest) -> bool:
    """Read a v4 single-document manifest and populate `manifest`.

    Always harvests skips (regardless of version). Populates comics /
    latest only when version == 4. Returns True when the file existed
    and parsed as JSON, so the caller knows to delete it.
    """
    if not path.exists():
        return False
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return False
    if not isinstance(document, dict):
        return True
    _ingest_skipped_list(document.get("skipped", []), manifest)
    if document.get("version") != 4:
        return True
    latest_raw = document.get("latest")
    if (
        isinstance(latest_raw, int)
        and not isinstance(latest_raw, bool)
        and latest_raw > 0
    ):
        manifest.latest = latest_raw
    comics_raw = document.get("comics", {})
    if isinstance(comics_raw, dict):
        for key, value in comics_raw.items():
            try:
                number = int(key)
            except (TypeError, ValueError):
                continue
            if number <= 0 or number == 404:
                continue
            _ingest_comic_dict(value, manifest, number)
    return True


def _load_v5_jsonl(path: Path, manifest: Manifest) -> None:
    """Read a v5 JSONL manifest and populate `manifest` in place."""
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:
        return
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            obj = json.loads(line)
        except ValueError:
            # Ignore malformed lines rather than discarding the whole
            # manifest — the firmware is stricter, but this tool's job
            # is to recover as much state as possible before re-writing.
            continue
        if not isinstance(obj, dict):
            continue
        # Header line: no "n", carries version/latest/skips.
        if "n" not in obj:
            if obj.get("v") != CACHE_INDEX_VERSION:
                # Stale header from a mismatched version; ignore comics
                # from this file entirely and let the caller re-download.
                manifest.comics.clear()
                manifest.latest = 0
                # Skips are still useful across upgrades.
                _ingest_skipped_list(obj.get("s", []), manifest)
                return
            latest_raw = obj.get("l")
            if (
                isinstance(latest_raw, int)
                and not isinstance(latest_raw, bool)
                and latest_raw > 0
            ):
                manifest.latest = latest_raw
            _ingest_skipped_list(obj.get("s", []), manifest)
            continue
        # Comic line.
        number = obj.get("n")
        if not isinstance(number, int) or isinstance(number, bool):
            continue
        if number <= 0 or number == 404:
            continue
        _ingest_comic_dict(obj, manifest, number)


def load_manifest(cache_dir: Path) -> Manifest:
    """Load the persisted manifest, migrating from legacy layouts.

    Handles all upgrade paths transparently: v5 JSONL (native), v4 single-doc
    JSON, v3 JSON (cached list only, per-comic .json ingest), pre-JSON txt
    (skips only), stray .skip markers. Leaves the cache dir in a state where
    only image files and index.jsonl remain.
    """
    manifest = Manifest()
    manifest.skipped |= _adopt_legacy_skip_markers(cache_dir)

    index_path = cache_dir / CACHE_INDEX_NAME
    if index_path.exists():
        _load_v5_jsonl(index_path, manifest)
    else:
        # Try the retired v4 single-document format next.
        legacy_json = cache_dir / CACHE_INDEX_LEGACY_JSON
        if not _load_v4_json(legacy_json, manifest):
            # Nothing usable yet — try the pre-JSON text index for skips.
            manifest.skipped |= _read_legacy_txt_index(cache_dir / CACHE_INDEX_LEGACY_TXT)

    # Absorb any leftover per-comic .json files (pre-v4 layout).
    _ingest_legacy_per_comic_json(cache_dir, manifest)
    # Drop retired sibling files so the formats never coexist.
    (cache_dir / CACHE_INDEX_LEGACY_TXT).unlink(missing_ok=True)
    (cache_dir / CACHE_INDEX_LEGACY_JSON).unlink(missing_ok=True)
    return manifest


def write_manifest(cache_dir: Path, manifest: Manifest) -> None:
    atomic_write(cache_dir / CACHE_INDEX_NAME, encode_manifest(manifest))
    # Belt and braces: strip any legacy siblings that might have been
    # recreated between load and write.
    (cache_dir / CACHE_INDEX_LEGACY_TXT).unlink(missing_ok=True)
    (cache_dir / CACHE_INDEX_LEGACY_LATEST).unlink(missing_ok=True)
    (cache_dir / CACHE_INDEX_LEGACY_JSON).unlink(missing_ok=True)


def download_image(
    url: str, destination: Path, extension: str, timeout: float, retries: int
) -> None:
    last_error: Exception | None = None
    for attempt in range(1, retries + 1):
        descriptor, temporary_name = tempfile.mkstemp(
            prefix=f".{destination.name}.", suffix=".tmp", dir=destination.parent
        )
        temporary = Path(temporary_name)
        try:
            with os.fdopen(descriptor, "wb") as output, request(url, timeout) as response:
                while chunk := response.read(CHUNK_SIZE):
                    output.write(chunk)
                output.flush()
                os.fsync(output.fileno())
            if not valid_image_header(temporary, extension):
                raise DownloadError(f"{url}: response is not a valid {extension} image")
            os.replace(temporary, destination)
            return
        except urllib.error.HTTPError as exc:
            last_error = exc
            if exc.code not in {408, 425, 429, 500, 502, 503, 504}:
                temporary.unlink(missing_ok=True)
                break
        except (urllib.error.URLError, TimeoutError, OSError, DownloadError) as exc:
            last_error = exc
            if isinstance(exc, OSError) and exc.errno == errno.ENOSPC:
                temporary.unlink(missing_ok=True)
                raise
        temporary.unlink(missing_ok=True)
        if attempt < retries:
            time.sleep(retry_delay(attempt))
    raise DownloadError(f"{url}: {last_error}") from last_error


def process_comic(
    number: int,
    cache_dir: Path,
    timeout: float,
    retries: int,
    force: bool,
    stop_event: threading.Event,
    manifest_snapshot: dict[int, ComicMeta],
    skipped_snapshot: frozenset[int],
) -> Result:
    if stop_event.is_set():
        return Result(number, "cancelled", "stopped")
    if number == 404:
        return Result(number, "skipped", "XKCD #404 intentionally does not exist")

    if not force and number in skipped_snapshot:
        return Result(number, "skipped", "previously marked non-retriable")

    known = None if force else manifest_snapshot.get(number)
    metadata_downloaded = False

    try:
        if known is not None:
            meta = known
        else:
            raw_metadata = fetch_bytes(
                COMIC_METADATA_URL.format(number=number), timeout, retries
            )
            decoded = decode_metadata(raw_metadata, number)
            meta = meta_from_xkcd_json(decoded)
            metadata_downloaded = True
            if meta is None:
                return Result(number, "skipped", "unsupported image extension")

        image_path = cache_dir / f"{number}{meta.extension}"
        if not force and valid_image_header(image_path, meta.extension):
            detail = "metadata refreshed; image already cached" if metadata_downloaded else "cached"
            return Result(number, "cached", detail, meta=meta)

        download_image(meta.url, image_path, meta.extension, timeout, retries)
        return Result(number, "downloaded", image_path.name, meta=meta)
    except OSError as exc:
        if exc.errno == errno.ENOSPC:
            stop_event.set()
            return Result(number, "failed", "SD card is out of space")
        return Result(number, "failed", str(exc))
    except (DownloadError, ValueError) as exc:
        return Result(number, "failed", str(exc))


def load_latest(cache_dir: Path, timeout: float, retries: int,
                manifest: Manifest) -> tuple[dict[str, Any] | None, int]:
    """Fetch the latest comic's metadata. Returns (raw_decoded, number).

    Falls back to the manifest's stored latest number if the network
    call fails. raw_decoded is None on cache-only fallback.
    """
    try:
        raw = fetch_bytes(LATEST_URL, timeout, retries)
        decoded = decode_metadata(raw)
        return decoded, int(decoded["num"])
    except (DownloadError, ValueError) as live_error:
        if manifest.latest > 0:
            print(
                f"Warning: latest lookup failed; resuming from manifest #{manifest.latest} ({live_error})",
                file=sys.stderr,
            )
            return None, manifest.latest
        raise DownloadError(
            f"could not retrieve latest XKCD and no cached manifest exists: {live_error}"
        ) from live_error


def install_fonts(sd_root: Path, ttf_path: Path, sizes: Iterable[int] = FONT_SIZES_PX) -> None:
    """Generate /fonts/xkcd_<size>.vlw on the SD card from a TTF.

    Imports build_vlw() from make_vlw.py so a Pillow/fontTools install is
    still required, but no external tool run.
    """
    from make_vlw import build_vlw  # local import: avoid Pillow dep unless requested

    if not ttf_path.is_file():
        raise FileNotFoundError(f"font source not found: {ttf_path}")
    fonts_dir = sd_root / "fonts"
    fonts_dir.mkdir(exist_ok=True)
    for size in sizes:
        out_path = fonts_dir / f"xkcd_{size}.vlw"
        print(f"Fonts:     building {out_path.name} from {ttf_path.name}...", flush=True)
        data = build_vlw(ttf_path, size)
        tmp = out_path.with_suffix(out_path.suffix + ".tmp")
        tmp.write_bytes(data)
        tmp.replace(out_path)
        print(f"           wrote {out_path} ({len(data):,} bytes)")


def main() -> int:
    args = parse_args()
    sd_root = args.sd_root.expanduser().resolve()
    if not sd_root.is_dir():
        print(f"Error: SD-card root does not exist or is not a directory: {sd_root}", file=sys.stderr)
        return 2

    cache_dir = sd_root / "xkcd"
    try:
        cache_dir.mkdir(exist_ok=True)
    except OSError as exc:
        print(f"Error: cannot create {cache_dir}: {exc}", file=sys.stderr)
        return 2

    free_bytes = shutil.disk_usage(sd_root).free
    print(f"SD root:   {sd_root}")
    print(f"Cache:     {cache_dir}")
    print(f"Free space: {free_bytes / (1024 ** 3):.2f} GiB")

    if args.with_fonts:
        try:
            install_fonts(sd_root, args.fonts_ttf.expanduser().resolve())
        except (FileNotFoundError, OSError, ImportError) as exc:
            print(f"Error: font install failed: {exc}", file=sys.stderr)
            return 2

    manifest = load_manifest(cache_dir)
    if args.force:
        # Re-evaluate every comic and every skip verdict.
        manifest.comics.clear()
        manifest.skipped.clear()

    manifest_lock = threading.Lock()

    try:
        latest_raw, latest_number = load_latest(
            cache_dir, args.timeout, args.retries, manifest
        )
    except (DownloadError, OSError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1

    manifest.latest = latest_number
    if latest_raw is not None:
        latest_meta = meta_from_xkcd_json(latest_raw)
        if latest_meta is not None and (args.force or latest_number not in manifest.comics):
            # Prime the manifest so process_comic can skip its own metadata fetch.
            manifest.comics[latest_number] = latest_meta

    end = min(args.end, latest_number) if args.end is not None else latest_number
    numbers = [number for number in range(args.start, end + 1) if number != 404]
    print(f"Latest:    XKCD #{latest_number}")
    print(
        f"Range:     #{args.start} to #{end} ({len(numbers)} comics; #404 excluded)"
    )
    print(f"Workers:   {args.workers}")

    counts = {"downloaded": 0, "cached": 0, "skipped": 0, "failed": 0, "cancelled": 0}
    stop_event = threading.Event()

    # Snapshot the manifest state that workers need. Workers don't mutate
    # shared state; the main thread folds their Results back into the
    # manifest under the lock as they complete.
    with manifest_lock:
        comics_snapshot = dict(manifest.comics)
        skipped_snapshot = frozenset(manifest.skipped)

    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as executor:
            futures = {
                executor.submit(
                    process_comic,
                    number,
                    cache_dir,
                    args.timeout,
                    args.retries,
                    args.force,
                    stop_event,
                    comics_snapshot,
                    skipped_snapshot,
                ): number
                for number in numbers
            }
            completed = 0
            for future in concurrent.futures.as_completed(futures):
                result = future.result()
                completed += 1
                counts[result.status] += 1
                with manifest_lock:
                    if result.status in {"downloaded", "cached"} and result.meta is not None:
                        manifest.comics[result.number] = result.meta
                    elif result.status == "skipped" and result.detail == "unsupported image extension":
                        manifest.skipped.add(result.number)

                if result.status in {"failed", "skipped"}:
                    print(
                        f"[{completed}/{len(numbers)}] #{result.number}: "
                        f"{result.status} - {result.detail}"
                    )
                elif completed % 25 == 0 or result.status == "downloaded":
                    print(
                        f"[{completed}/{len(numbers)}] #{result.number}: {result.status}"
                    )

                if stop_event.is_set():
                    for pending in futures:
                        pending.cancel()
                    break
    except KeyboardInterrupt:
        stop_event.set()
        try:
            with manifest_lock:
                write_manifest(cache_dir, manifest)
            print(f"Updated cache index with {len(manifest.comics)} complete comics.")
        except (OSError, ValueError) as exc:
            print(f"Warning: could not update cache index: {exc}", file=sys.stderr)
        print("\nInterrupted; completed files are safe and the command can be rerun.", file=sys.stderr)
        return 130

    print(
        "Summary: "
        + ", ".join(f"{name}={count}" for name, count in counts.items() if count)
    )
    try:
        with manifest_lock:
            write_manifest(cache_dir, manifest)
            indexed_count = len(manifest.comics)
            skip_count = len(manifest.skipped)
        print(
            f"Index:     {indexed_count} complete comics, {skip_count} skipped"
        )
    except (OSError, ValueError) as exc:
        print(f"Error: could not update cache index: {exc}", file=sys.stderr)
        return 1
    if stop_event.is_set():
        print("Stopped because the SD card ran out of space.", file=sys.stderr)
        return 1
    if counts["failed"]:
        print("Some downloads failed. Rerun the command to retry them.", file=sys.stderr)
        return 1

    print("XKCD cache is ready. Safely eject the SD card before removing it.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
