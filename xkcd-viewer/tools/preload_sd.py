#!/usr/bin/env python3
"""Pre-populate an SD card with the complete XKCD archive.

The output matches the cache layout used by the XKCD Viewer firmware:

    <SD root>/xkcd/latest.json
    <SD root>/xkcd/1.json
    <SD root>/xkcd/1.png
    ...

The command is resumable. Valid metadata and image files already on the card
are not downloaded again unless --force is used.
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
from dataclasses import dataclass
from pathlib import Path
from typing import Any


LATEST_URL = "https://xkcd.com/info.0.json"
COMIC_METADATA_URL = "https://xkcd.com/{number}/info.0.json"
SUPPORTED_EXTENSIONS = {".png", ".jpg", ".jpeg", ".bmp"}
USER_AGENT = (
    "seeed-reterminal-E100X-xkcd-preloader/1.0 "
    "(https://github.com/danpodeanu/seeed-reterminal-E100X)"
)
CHUNK_SIZE = 128 * 1024


class DownloadError(RuntimeError):
    """A download failed after all permitted attempts."""


@dataclass(frozen=True)
class Result:
    number: int
    status: str
    detail: str


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


def read_valid_metadata(path: Path, expected_number: int) -> dict[str, Any] | None:
    try:
        return decode_metadata(path.read_bytes(), expected_number)
    except (OSError, ValueError):
        return None


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
) -> Result:
    if stop_event.is_set():
        return Result(number, "cancelled", "stopped")
    if number == 404:
        return Result(number, "skipped", "XKCD #404 intentionally does not exist")

    metadata_path = cache_dir / f"{number}.json"
    metadata = None if force else read_valid_metadata(metadata_path, number)
    metadata_downloaded = metadata is None

    try:
        if metadata is None:
            raw_metadata = fetch_bytes(
                COMIC_METADATA_URL.format(number=number), timeout, retries
            )
            metadata = decode_metadata(raw_metadata, number)
            atomic_write(metadata_path, raw_metadata)

        extension = image_extension(metadata["img"])
        if not extension:
            return Result(number, "skipped", "unsupported image extension")

        image_path = cache_dir / f"{number}{extension}"
        if not force and valid_image_header(image_path, extension):
            detail = "metadata refreshed; image already cached" if metadata_downloaded else "cached"
            return Result(number, "cached", detail)

        download_image(metadata["img"], image_path, extension, timeout, retries)
        return Result(number, "downloaded", image_path.name)
    except OSError as exc:
        if exc.errno == errno.ENOSPC:
            stop_event.set()
            return Result(number, "failed", "SD card is out of space")
        return Result(number, "failed", str(exc))
    except (DownloadError, ValueError) as exc:
        return Result(number, "failed", str(exc))


def load_latest(cache_dir: Path, timeout: float, retries: int) -> tuple[dict[str, Any], bytes]:
    try:
        raw = fetch_bytes(LATEST_URL, timeout, retries)
        return decode_metadata(raw), raw
    except (DownloadError, ValueError) as live_error:
        cached_path = cache_dir / "latest.json"
        try:
            raw = cached_path.read_bytes()
            metadata = decode_metadata(raw)
        except (OSError, ValueError) as cache_error:
            raise DownloadError(
                f"could not retrieve latest XKCD and no valid cache exists: {live_error}"
            ) from cache_error
        print(f"Warning: latest lookup failed; resuming from {cached_path}", file=sys.stderr)
        return metadata, raw


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

    try:
        latest, raw_latest = load_latest(cache_dir, args.timeout, args.retries)
        latest_number = int(latest["num"])
        atomic_write(cache_dir / "latest.json", raw_latest)
        atomic_write(cache_dir / f"{latest_number}.json", raw_latest)
    except (DownloadError, OSError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1

    end = min(args.end, latest_number) if args.end is not None else latest_number
    numbers = [number for number in range(args.start, end + 1) if number != 404]
    print(f"Latest:    XKCD #{latest_number}")
    print(
        f"Range:     #{args.start} to #{end} ({len(numbers)} comics; #404 excluded)"
    )
    print(f"Workers:   {args.workers}")

    counts = {"downloaded": 0, "cached": 0, "skipped": 0, "failed": 0, "cancelled": 0}
    stop_event = threading.Event()

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
                ): number
                for number in numbers
            }
            completed = 0
            for future in concurrent.futures.as_completed(futures):
                result = future.result()
                completed += 1
                counts[result.status] += 1
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
        print("\nInterrupted; completed files are safe and the command can be rerun.", file=sys.stderr)
        return 130

    print(
        "Summary: "
        + ", ".join(f"{name}={count}" for name, count in counts.items() if count)
    )
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
