#!/usr/bin/env python3
"""Verify weather-viewer credentials against the live provider APIs.

Reads ``include/secrets.h`` for QWeather credentials and ``include/config.h``
for the LATITUDE / LONGITUDE the firmware would use, then makes one API call
per provider so you can confirm the values you flashed will actually work
before deploying to the reTerminal:

* QWeather -- signs an Ed25519 JWT the same way the firmware does and calls
  ``GET /v7/weather/now``. Prints the returned temperature on success or the
  API's ``code`` + ``fxLink`` on failure.
* Open-Meteo -- makes an anonymous ``current_weather=true`` request and
  prints the returned temperature. No credentials required.

Run from ``weather-viewer/`` (or point ``--include-dir`` at another
weather-viewer include directory).

Requires ``cryptography`` for JWT signing::

    pip install cryptography
"""

from __future__ import annotations

import argparse
import base64
import binascii
import json
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Dict, Optional


def _parse_defines(header_path: Path) -> Dict[str, str]:
    """Extract ``#define NAME "value"`` entries from a header file.

    Supports line continuations (``\\``) so multi-line hex keys are joined
    into a single value. Numeric ``#define``s are ignored -- we only care
    about string macros here.
    """
    if not header_path.is_file():
        raise FileNotFoundError(header_path)

    text = header_path.read_text(encoding="utf-8")
    text = re.sub(r"\\\r?\n\s*", "", text)  # join line continuations
    pattern = re.compile(r'^\s*#define\s+(\w+)\s+"([^"]*)"\s*$', re.MULTILINE)
    return {m.group(1): m.group(2) for m in pattern.finditer(text)}


def _parse_config_coords(config_path: Path) -> Dict[str, float]:
    """Pull LATITUDE / LONGITUDE out of the firmware's config.h."""
    if not config_path.is_file():
        raise FileNotFoundError(config_path)

    text = config_path.read_text(encoding="utf-8")
    out: Dict[str, float] = {}
    for name in ("LATITUDE", "LONGITUDE"):
        match = re.search(
            rf"\b{name}\s*=\s*([-+]?\d+(?:\.\d+)?)", text)
        if match:
            out[name] = float(match.group(1))
    return out


def _base64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode("ascii")


def _build_qweather_jwt(sub: str, kid: str, private_key_hex: str,
                        lifetime_seconds: int = 15 * 60) -> str:
    try:
        from cryptography.hazmat.primitives.asymmetric.ed25519 import (
            Ed25519PrivateKey,
        )
    except ImportError as exc:  # pragma: no cover - depends on the host env
        raise SystemExit(
            "[qweather] `cryptography` is not installed. "
            "Run: pip install cryptography") from exc

    try:
        cleaned = re.sub(r"[\s:_-]", "", private_key_hex)
        if cleaned.lower().startswith("0x"):
            cleaned = cleaned[2:]
        seed = binascii.unhexlify(cleaned)
    except binascii.Error as exc:
        raise SystemExit(
            f"[qweather] QWEATHER_PRIVATE_KEY_HEX is not valid hex: {exc}"
        )
    if len(seed) != 32:
        raise SystemExit(
            "[qweather] QWEATHER_PRIVATE_KEY_HEX must decode to 32 bytes "
            f"(got {len(seed)})")

    private_key = Ed25519PrivateKey.from_private_bytes(seed)

    now = int(time.time())
    header = {"alg": "EdDSA", "kid": kid}
    # Match the firmware: iat back-dated 30s, exp = iat + 15 min.
    payload = {"sub": sub, "iat": now - 30, "exp": now - 30 + lifetime_seconds}

    encoded_header = _base64url(
        json.dumps(header, separators=(",", ":")).encode())
    encoded_payload = _base64url(
        json.dumps(payload, separators=(",", ":")).encode())
    signing_input = f"{encoded_header}.{encoded_payload}".encode("ascii")
    signature = _base64url(private_key.sign(signing_input))
    return f"{encoded_header}.{encoded_payload}.{signature}"


def _safe_print(*parts: str) -> None:
    """Print without blowing up on non-ASCII when stdout is cp1252 (Win)."""
    encoding = sys.stdout.encoding or "utf-8"
    line = " ".join(parts)
    try:
        print(line)
    except UnicodeEncodeError:
        print(line.encode(encoding, errors="replace").decode(encoding))


def _decode_body(raw: bytes, content_encoding: str) -> str:
    """Return the body as UTF-8 text, decompressing if gzip/deflate."""
    encoding = (content_encoding or "").lower().strip()
    if encoding == "gzip":
        import gzip
        try:
            raw = gzip.decompress(raw)
        except OSError:
            pass
    elif encoding == "deflate":
        import zlib
        try:
            raw = zlib.decompress(raw)
        except zlib.error:
            pass
    return raw.decode("utf-8", errors="replace")


def _http_get_json(url: str, headers: Optional[Dict[str, str]] = None,
                   timeout: float = 10.0) -> Dict:
    merged_headers = {
        "Accept-Encoding": "gzip, deflate",
        "User-Agent": "reterminal-weather-tester/1.0",
    }
    merged_headers.update(headers or {})
    request = urllib.request.Request(url, headers=merged_headers)
    with urllib.request.urlopen(request, timeout=timeout) as response:
        text = _decode_body(response.read(),
                            response.headers.get("Content-Encoding", ""))
    return json.loads(text)


def test_qweather(secrets: Dict[str, str], latitude: float,
                  longitude: float) -> bool:
    print("[qweather] checking credentials...")
    required = ("QWEATHER_API_HOST", "QWEATHER_PROJECT_ID", "QWEATHER_CREDENTIAL_ID",
                "QWEATHER_PRIVATE_KEY_HEX")
    missing = [k for k in required if not secrets.get(k)]
    if missing:
        print(f"[qweather] SKIP -- missing in secrets.h: {', '.join(missing)}")
        return False

    try:
        jwt = _build_qweather_jwt(
            sub=secrets["QWEATHER_PROJECT_ID"],
            kid=secrets["QWEATHER_CREDENTIAL_ID"],
            private_key_hex=secrets["QWEATHER_PRIVATE_KEY_HEX"],
        )
    except SystemExit as exc:
        print(str(exc))
        return False

    host = secrets["QWEATHER_API_HOST"]
    # QWeather takes location as "lon,lat" with up to two decimals.
    location = f"{longitude:.2f},{latitude:.2f}"
    url = (f"https://{host}/v7/weather/now?"
           + urllib.parse.urlencode({"location": location}))
    print(f"[qweather] GET {url}")
    try:
        body = _http_get_json(url, headers={"Authorization": f"Bearer {jwt}"})
    except urllib.error.HTTPError as exc:
        detail = _decode_body(exc.read(),
                              exc.headers.get("Content-Encoding", ""))
        _safe_print(f"[qweather] FAIL -- HTTP {exc.code}: {detail[:400]}")
        return False
    except urllib.error.URLError as exc:
        print(f"[qweather] FAIL -- network error: {exc.reason}")
        return False

    code = body.get("code")
    if code != "200":
        print(f"[qweather] FAIL -- code={code} "
              f"(fxLink={body.get('fxLink')}) body={body}")
        return False

    now = body.get("now", {})
    print(f"[qweather] OK -- {now.get('temp')}C {now.get('text')} "
          f"(obsTime={now.get('obsTime')})")
    return True


def test_open_meteo(latitude: float, longitude: float) -> bool:
    print("[open-meteo] checking public API...")
    url = ("https://api.open-meteo.com/v1/forecast?"
           + urllib.parse.urlencode({
               "latitude": f"{latitude}",
               "longitude": f"{longitude}",
               "current_weather": "true",
           }))
    print(f"[open-meteo] GET {url}")
    try:
        body = _http_get_json(url)
    except urllib.error.HTTPError as exc:
        detail = _decode_body(exc.read(),
                              exc.headers.get("Content-Encoding", ""))
        _safe_print(f"[open-meteo] FAIL -- HTTP {exc.code}: {detail[:400]}")
        return False
    except urllib.error.URLError as exc:
        print(f"[open-meteo] FAIL -- network error: {exc.reason}")
        return False

    current = body.get("current_weather") or {}
    if "temperature" not in current:
        print(f"[open-meteo] FAIL -- no current_weather in response: {body}")
        return False
    print(f"[open-meteo] OK -- {current['temperature']}C "
          f"(time={current.get('time')})")
    return True


def main(argv: Optional[list] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Verify weather-viewer credentials against live APIs.")
    parser.add_argument(
        "--include-dir", type=Path, default=Path("include"),
        help="Path to the weather-viewer include/ directory "
             "(default: ./include, run from weather-viewer/).")
    args = parser.parse_args(argv)

    secrets_path = args.include_dir / "secrets.h"
    config_path = args.include_dir / "config.h"

    try:
        secrets = _parse_defines(secrets_path)
    except FileNotFoundError:
        print(f"[error] {secrets_path} not found. Copy secrets.h.example to "
              f"secrets.h and fill it in first.")
        return 2

    try:
        coords = _parse_config_coords(config_path)
    except FileNotFoundError:
        print(f"[error] {config_path} not found.")
        return 2

    latitude = coords.get("LATITUDE")
    longitude = coords.get("LONGITUDE")
    if latitude is None or longitude is None:
        print(f"[error] could not find LATITUDE / LONGITUDE in {config_path}")
        return 2
    print(f"[config] latitude={latitude} longitude={longitude}")

    qweather_ok = test_qweather(secrets, latitude, longitude)
    open_meteo_ok = test_open_meteo(latitude, longitude)

    print()
    print(f"[summary] qweather   = {'ok' if qweather_ok else 'FAIL'}")
    print(f"[summary] open-meteo = {'ok' if open_meteo_ok else 'FAIL'}")
    return 0 if (qweather_ok and open_meteo_ok) else 1


if __name__ == "__main__":
    sys.exit(main())
