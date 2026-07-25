"""Python mirror of common/include/secrets_normalise.h.

This module exists so any tester (weather, future xkcd auth, ...) can share
one implementation of the hex-digit normaliser, and so a parity self-test can
enforce it stays in lockstep with the firmware. The rules are simple by
design:

* Strip ASCII whitespace and ``:`` byte separators (from ``openssl priv:`` and
  similar dumps).
* Strip a single leading ``0x`` / ``0X`` prefix.
* Accept only ``[0-9a-fA-F]`` after that; anything else is rejected.

Note that ``_`` and ``-`` are NOT accepted. Some people copy hex strings from
ssh-style key dumps that use those separators; both firmware and tester must
refuse them consistently. Testers should call ``self_test()`` on every
invocation before hitting a real API so drift is caught loudly.
"""

from __future__ import annotations


_HEX_ALPHABET = "0123456789abcdefABCDEF"
_ALLOWED_SEPARATORS = " \t\n\r:"


def normalize_hex_digits(text: str) -> str:
    """Return the cleaned hex string, or raise ValueError for bad input."""
    if text.lower().startswith("0x"):
        text = text[2:]
    cleaned_chars = []
    for ch in text:
        if ch in _ALLOWED_SEPARATORS:
            continue
        if ch in _HEX_ALPHABET:
            cleaned_chars.append(ch)
            continue
        raise ValueError(
            f"unexpected character {ch!r}; only hex digits, "
            "ASCII whitespace, and ':' are allowed"
        )
    return "".join(cleaned_chars)


# Fixtures used by every tester's self-test. Keep in sync with the C++
# unity test `test_normalize_hex_digits_strips_common_key_formats` in
# weather-viewer/test/test_app_logic/test_main.cpp.
ACCEPT_FIXTURES: list[tuple[str, str]] = [
    ("94:d1:33", "94d133"),
    ("  94 d1\n33\t94\r\n1e ec 4d c5  ", "94d133941eec4dc5"),
    ("0xdeadbeef", "deadbeef"),
    ("0Xdeadbeef", "deadbeef"),
    ("", ""),
    (
        "94:d1:33:94:1e:ec:4d:c5:de:f2:b8:ff:76:01:ee:06:"
        "30:eb:38:20:1b:1b:b0:3a:23:16:f2:5f:fa:4c:bd:81",
        "94d133941eec4dc5def2b8ff7601ee06"
        "30eb38201b1bb03a2316f25ffa4cbd81",
    ),
]

REJECT_FIXTURES: list[str] = [
    "94g1",
    "94,1",
    "94_d1",
    "94-d1",
]


def self_test() -> None:
    """Assert parity with the firmware normaliser fixture set.

    Raises AssertionError with a "parity drift" prefix on any mismatch so
    testers can print the message verbatim and abort before any live API
    call is made.
    """
    for raw, expected in ACCEPT_FIXTURES:
        got = normalize_hex_digits(raw)
        assert got == expected, (
            f"parity drift: normalize_hex_digits({raw!r}) = {got!r}, "
            f"expected {expected!r}"
        )
    for raw in REJECT_FIXTURES:
        try:
            normalize_hex_digits(raw)
        except ValueError:
            continue
        raise AssertionError(
            f"parity drift: normalize_hex_digits({raw!r}) accepted an "
            "input the firmware normaliser would reject"
        )
