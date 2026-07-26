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

# common/tools/secrets_normalise.py owns the single source of truth for the
# hex-digit normaliser rules that also live in
# common/include/secrets_normalise.h. Import it via a path shim so this
# script keeps running when invoked with just "python tools/test_credentials.py"
# from the weather-viewer directory (no PYTHONPATH gymnastics required).
_REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(_REPO_ROOT / "common" / "tools"))
import secrets_normalise as _secrets_normalise  # noqa: E402


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


def _parse_config_string(config_path: Path, name: str) -> Optional[str]:
    """Extract ``constexpr char NAME[] = "..."`` from config.h."""
    if not config_path.is_file():
        return None
    text = config_path.read_text(encoding="utf-8")
    match = re.search(
        rf'\b{name}\s*\[\s*\]\s*=\s*"([^"]*)"', text)
    return match.group(1) if match else None


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


def _normalize_hex_digits(text: str) -> str:
    """Thin passthrough to the shared parity-checked normaliser.

    Rules live in ``common/tools/secrets_normalise.py`` (mirroring
    ``common/include/secrets_normalise.h``). ``_self_test()`` below enforces
    firmware/tester parity on every invocation.
    """
    return _secrets_normalise.normalize_hex_digits(text)


def _base64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode("ascii")


def _build_qweather_jwt(sub: str, kid: str, private_key_hex: str,
                        lifetime_seconds: int = 2 * 60 * 60,
                        iat_offset_seconds: int = 0) -> str:
    try:
        from cryptography.hazmat.primitives.asymmetric.ed25519 import (
            Ed25519PrivateKey,
        )
    except ImportError as exc:  # pragma: no cover - depends on the host env
        raise SystemExit(
            "[qweather] `cryptography` is not installed. "
            "Run: pip install cryptography") from exc

    try:
        cleaned = _normalize_hex_digits(private_key_hex)
        seed = binascii.unhexlify(cleaned)
    except (ValueError, binascii.Error) as exc:
        raise SystemExit(
            f"[qweather] QWEATHER_PRIVATE_KEY_HEX is not valid hex: {exc}"
        )
    if len(seed) != 32:
        raise SystemExit(
            "[qweather] QWEATHER_PRIVATE_KEY_HEX must decode to 32 bytes "
            f"(got {len(seed)})")

    private_key = Ed25519PrivateKey.from_private_bytes(seed)

    now = int(time.time()) + iat_offset_seconds
    header = {"alg": "EdDSA", "kid": kid}
    # Match the firmware: iat back-dated 300s, exp = iat + 2 h.
    payload = {"sub": sub, "iat": now - 300,
               "exp": now - 300 + lifetime_seconds}

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


def _qweather_severity_rank(severity: str) -> int:
    """Mirror app_logic::qweatherAlertSeverityRank so the tester picks the
    same 'top' alert the firmware would display."""
    return {"Extreme": 4, "Severe": 3, "Moderate": 2, "Minor": 1}.get(
        severity or "", 0)


def test_qweather(secrets: Dict[str, str], latitude: float,
                  longitude: float, lang: str,
                  iat_offset_seconds: int = 0) -> bool:
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
            iat_offset_seconds=iat_offset_seconds,
        )
    except SystemExit as exc:
        print(str(exc))
        return False
    if iat_offset_seconds:
        print(f"[qweather] using iat offset {iat_offset_seconds:+d}s "
              f"from wall clock")

    host = secrets["QWEATHER_API_HOST"]
    # QWeather takes location as "lon,lat" with up to two decimals.
    location = f"{longitude:.2f},{latitude:.2f}"
    url = (f"https://{host}/v7/weather/now?"
           + urllib.parse.urlencode(
               {"location": location, "unit": "m", "lang": lang}))
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
    _safe_print(f"[qweather] OK -- {now.get('temp')}C {now.get('text')} "
                f"(obsTime={now.get('obsTime')})")

    # Best-effort probe of the warnings endpoint used by the firmware alert
    # feature. A 200 with an empty ``warning`` array is the normal case for
    # most locations; any HTTP error here is logged but must not fail the
    # credentials check.
    warning_url = (f"https://{host}/v7/warning/now?"
                   + urllib.parse.urlencode(
                       {"location": location, "lang": lang}))
    print(f"[qweather] GET {warning_url}")
    try:
        warning_body = _http_get_json(
            warning_url,
            headers={"Authorization": f"Bearer {jwt}"})
    except urllib.error.HTTPError as exc:
        detail = _decode_body(exc.read(),
                              exc.headers.get("Content-Encoding", ""))
        _safe_print(f"[qweather] warning FAIL -- HTTP {exc.code}: "
                    f"{detail[:200]}")
        return True
    except urllib.error.URLError as exc:
        print(f"[qweather] warning FAIL -- network error: {exc.reason}")
        return True
    if warning_body.get("code") != "200":
        print(f"[qweather] warning code={warning_body.get('code')} (ignored)")
        return True
    warnings_list = warning_body.get("warning") or []
    if not warnings_list:
        print("[qweather] warning OK -- no active alerts")
    else:
        top = max(warnings_list,
                  key=lambda w: _qweather_severity_rank(w.get("severity", "")))
        extras = len(warnings_list) - 1
        suffix = f" (+{extras} more)" if extras > 0 else ""
        _safe_print(
            f"[qweather] warning OK -- "
            f"{top.get('severity') or 'Unknown'}: "
            f"{top.get('title', '')}{suffix}")
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


def test_nws_alerts(latitude: float, longitude: float) -> bool:
    """Probe the US NWS alerts endpoint that the firmware calls when
    ``NWS_ALERTS_ENABLED`` is true and the active provider is Open-Meteo.
    Coverage is US-only, so a request from outside the US returns an empty
    features list (or an HTTP error) -- treat either as "not applicable".
    Never fails the overall credentials check.
    """
    print("[nws] checking severe-weather alerts endpoint (US only)...")
    url = ("https://api.weather.gov/alerts/active?"
           + urllib.parse.urlencode(
               {"point": f"{latitude:.4f},{longitude:.4f}"}))
    print(f"[nws] GET {url}")
    try:
        body = _http_get_json(
            url,
            headers={
                "User-Agent":
                    "reterminal-weather-tester/1.0 "
                    "(https://github.com/danpodeanu/seeed-reterminal-E100X)",
                "Accept": "application/geo+json",
            })
    except urllib.error.HTTPError as exc:
        detail = _decode_body(exc.read(),
                              exc.headers.get("Content-Encoding", ""))
        _safe_print(f"[nws] HTTP {exc.code} (likely outside US coverage): "
                    f"{detail[:200]}")
        return True
    except urllib.error.URLError as exc:
        print(f"[nws] network error: {exc.reason}")
        return True

    features = body.get("features") or []
    if not features:
        print("[nws] OK -- no active alerts (or point outside US)")
        return True
    top = max(
        features,
        key=lambda f: _qweather_severity_rank(
            (f.get("properties") or {}).get("severity", "")))
    props = top.get("properties") or {}
    extras = len(features) - 1
    suffix = f" (+{extras} more)" if extras > 0 else ""
    _safe_print(
        f"[nws] OK -- {props.get('severity') or 'Unknown'}: "
        f"{props.get('event', '')}{suffix}")
    return True


# ----------------------------------------------------------------------------
# Cross-geography probe
# ----------------------------------------------------------------------------

# Three representative locations chosen so every quadrant of the
# (in China? / in US?) matrix is covered:
#   Shanghai -> in China, out US    (QWeather sweet spot)
#   New York -> out China, in US    (NWS sweet spot)
#   London   -> out China, out US   (neither national service applies)
# All three are inside Open-Meteo coverage (worldwide).
_GEO_LOCATIONS: tuple = (
    ("Shanghai",  31.2304, 121.4737, "in China, out US"),
    ("New York",  40.7128, -74.0060, "out China, in US"),
    ("London",    51.5074,  -0.1278, "out China, out US"),
)


def _probe_qweather_now(host: str, jwt: str, latitude: float,
                        longitude: float, lang: str) -> str:
    location = f"{longitude:.2f},{latitude:.2f}"
    url = (f"https://{host}/v7/weather/now?"
           + urllib.parse.urlencode(
               {"location": location, "unit": "m", "lang": lang}))
    try:
        body = _http_get_json(url, headers={"Authorization": f"Bearer {jwt}"})
    except urllib.error.HTTPError as exc:
        return f"HTTP {exc.code}"
    except urllib.error.URLError as exc:
        return f"net err: {exc.reason}"
    code = body.get("code")
    if code != "200":
        return f"code={code}"
    now = body.get("now", {})
    return f"OK {now.get('temp')}C {now.get('text', '')}".strip()


def _probe_qweather_warning(host: str, jwt: str, latitude: float,
                            longitude: float, lang: str) -> str:
    location = f"{longitude:.2f},{latitude:.2f}"
    url = (f"https://{host}/v7/warning/now?"
           + urllib.parse.urlencode(
               {"location": location, "lang": lang}))
    try:
        body = _http_get_json(url, headers={"Authorization": f"Bearer {jwt}"})
    except urllib.error.HTTPError as exc:
        return f"HTTP {exc.code}"
    except urllib.error.URLError as exc:
        return f"net err: {exc.reason}"
    code = body.get("code")
    if code != "200":
        return f"code={code}"
    warnings_list = body.get("warning") or []
    if not warnings_list:
        return "OK no alerts"
    top = max(warnings_list,
              key=lambda w: _qweather_severity_rank(w.get("severity", "")))
    extras = len(warnings_list) - 1
    suffix = f" (+{extras})" if extras > 0 else ""
    return (f"OK {top.get('severity') or 'Unknown'}: "
            f"{top.get('title', '')}{suffix}")


def _probe_open_meteo(latitude: float, longitude: float) -> str:
    url = ("https://api.open-meteo.com/v1/forecast?"
           + urllib.parse.urlencode({
               "latitude": f"{latitude}",
               "longitude": f"{longitude}",
               "current_weather": "true",
           }))
    try:
        body = _http_get_json(url)
    except urllib.error.HTTPError as exc:
        return f"HTTP {exc.code}"
    except urllib.error.URLError as exc:
        return f"net err: {exc.reason}"
    current = body.get("current_weather") or {}
    if "temperature" not in current:
        return "no current_weather"
    return f"OK {current['temperature']}C"


def _probe_nws(latitude: float, longitude: float) -> str:
    url = ("https://api.weather.gov/alerts/active?"
           + urllib.parse.urlencode(
               {"point": f"{latitude:.4f},{longitude:.4f}"}))
    try:
        body = _http_get_json(
            url,
            headers={
                "User-Agent":
                    "reterminal-weather-tester/1.0 "
                    "(https://github.com/danpodeanu/seeed-reterminal-E100X)",
                "Accept": "application/geo+json",
            })
    except urllib.error.HTTPError as exc:
        return f"HTTP {exc.code}"
    except urllib.error.URLError as exc:
        return f"net err: {exc.reason}"
    features = body.get("features") or []
    if not features:
        return "OK no alerts"
    top = max(
        features,
        key=lambda f: _qweather_severity_rank(
            (f.get("properties") or {}).get("severity", "")))
    props = top.get("properties") or {}
    extras = len(features) - 1
    suffix = f" (+{extras})" if extras > 0 else ""
    return (f"OK {props.get('severity') or 'Unknown'}: "
            f"{props.get('event', '')}{suffix}")


def run_geo_probe(secrets: Dict[str, str], lang: str,
                  iat_offset_seconds: int = 0) -> None:
    """Probe every provider from Shanghai / New York / London to show how
    each API behaves across the (China vs. US vs. neither) geography
    matrix. Purely informational -- results are printed as a table and
    the function never fails the overall tester.
    """
    print()
    print("=" * 78)
    print("[geo-probe] cross-geography API behaviour")
    print("=" * 78)

    # Build one JWT and reuse for every QWeather call in this run so
    # differences across locations reflect the server-side geo-fencing,
    # not clock drift between requests.
    required = ("QWEATHER_PROJECT_ID", "QWEATHER_CREDENTIAL_ID",
                "QWEATHER_PRIVATE_KEY_HEX", "QWEATHER_API_HOST")
    missing = [k for k in required if not secrets.get(k)]
    qweather_jwt: Optional[str] = None
    qweather_host: Optional[str] = None
    if missing:
        print(f"[geo-probe] QWeather columns skipped -- missing in "
              f"secrets.h: {', '.join(missing)}")
    else:
        try:
            qweather_jwt = _build_qweather_jwt(
                sub=secrets["QWEATHER_PROJECT_ID"],
                kid=secrets["QWEATHER_CREDENTIAL_ID"],
                private_key_hex=secrets["QWEATHER_PRIVATE_KEY_HEX"],
                iat_offset_seconds=iat_offset_seconds,
            )
            qweather_host = secrets["QWEATHER_API_HOST"]
        except SystemExit as exc:
            print(f"[geo-probe] QWeather columns skipped -- {exc}")

    rows = []
    for name, lat, lon, note in _GEO_LOCATIONS:
        print()
        print(f"[geo-probe] {name} ({lat:.4f}, {lon:.4f}) -- {note}")
        if qweather_jwt and qweather_host:
            qw_now = _probe_qweather_now(qweather_host, qweather_jwt,
                                         lat, lon, lang)
            qw_warn = _probe_qweather_warning(qweather_host, qweather_jwt,
                                              lat, lon, lang)
        else:
            qw_now = "skipped"
            qw_warn = "skipped"
        om = _probe_open_meteo(lat, lon)
        nws = _probe_nws(lat, lon)
        _safe_print(f"  qweather now      : {qw_now}")
        _safe_print(f"  qweather warning  : {qw_warn}")
        _safe_print(f"  open-meteo now    : {om}")
        _safe_print(f"  nws alerts        : {nws}")
        rows.append((name, qw_now, qw_warn, om, nws))

    print()
    print("[geo-probe] summary (truncated)")
    header = ("location", "qweather now", "qweather warn",
              "open-meteo", "nws alerts")
    widths = (10, 22, 22, 14, 22)

    def _fmt(row: tuple) -> str:
        return "  ".join(
            str(cell)[:w].ljust(w) for cell, w in zip(row, widths))

    print(_fmt(header))
    print(_fmt(tuple("-" * w for w in widths)))
    for row in rows:
        _safe_print(_fmt(row))
    print()


def _self_test() -> None:
    """Parity check between this tester and the firmware normaliser.

    Delegates to ``common/tools/secrets_normalise.self_test()``, which walks
    the ACCEPT_FIXTURES / REJECT_FIXTURES sets that are also asserted by the
    Unity test ``test_normalize_hex_digits_strips_common_key_formats``. On
    drift the shared self_test raises AssertionError with a "parity drift"
    prefix, aborting the tester before any live API call is made.
    """
    _secrets_normalise.self_test()


def main(argv: Optional[list] = None) -> int:
    _self_test()
    parser = argparse.ArgumentParser(
        description="Verify weather-viewer credentials against live APIs.")
    parser.add_argument(
        "--include-dir", type=Path, default=Path("include"),
        help="Path to the weather-viewer include/ directory "
             "(default: ./include, run from weather-viewer/).")
    parser.add_argument(
        "--dump-jwt", action="store_true",
        help="Print the JWT header, payload, and signature (base64url "
             "segments) that would be sent to QWeather. Use to compare "
             "against firmware output when DEBUG_LOG_JWT is enabled.")
    parser.add_argument(
        "--iat-offset", type=int, default=0, metavar="SECONDS",
        help="Add this offset to the current wall clock when generating "
             "the JWT iat/exp claims. Use negative to simulate a slow RTC "
             "(e.g. --iat-offset -3600 for one hour behind). Applies to "
             "both --dump-jwt and the live QWeather request.")
    parser.add_argument(
        "--no-geo-probe", action="store_true",
        help="Skip the cross-geography probe that runs every provider "
             "from Shanghai / New York / London to compare API behaviour "
             "inside vs. outside China and the US. The probe runs by "
             "default; pass this flag to keep runs fast when iterating on "
             "the configured-location checks only.")
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
    lang = _parse_config_string(config_path, "QWEATHER_LANG") or "en"
    print(f"[config] qweather lang={lang}")

    if args.dump_jwt:
        required = ("QWEATHER_PROJECT_ID", "QWEATHER_CREDENTIAL_ID",
                    "QWEATHER_PRIVATE_KEY_HEX")
        missing = [k for k in required if not secrets.get(k)]
        if missing:
            print(f"[dump-jwt] missing: {', '.join(missing)}")
            return 2
        now = int(time.time()) + args.iat_offset
        iat = now - 300
        exp = iat + 2 * 60 * 60
        jwt = _build_qweather_jwt(
            sub=secrets["QWEATHER_PROJECT_ID"],
            kid=secrets["QWEATHER_CREDENTIAL_ID"],
            private_key_hex=secrets["QWEATHER_PRIVATE_KEY_HEX"],
            iat_offset_seconds=args.iat_offset,
        )
        header, payload, signature = jwt.split(".")
        print(f"[dump-jwt] iat={iat} exp={exp} offset={args.iat_offset:+d}s")
        print(f"[dump-jwt] header={header}")
        print(f"[dump-jwt] payload={payload}")
        print(f"[dump-jwt] signature={signature}")
        print(f"[dump-jwt] full={jwt}")

    qweather_ok = test_qweather(secrets, latitude, longitude, lang,
                                iat_offset_seconds=args.iat_offset)
    open_meteo_ok = test_open_meteo(latitude, longitude)
    test_nws_alerts(latitude, longitude)

    if not args.no_geo_probe:
        run_geo_probe(secrets, lang, iat_offset_seconds=args.iat_offset)

    print()
    print(f"[summary] qweather   = {'ok' if qweather_ok else 'FAIL'}")
    print(f"[summary] open-meteo = {'ok' if open_meteo_ok else 'FAIL'}")
    return 0 if (qweather_ok and open_meteo_ok) else 1


if __name__ == "__main__":
    sys.exit(main())
