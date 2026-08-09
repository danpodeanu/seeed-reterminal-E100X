# Prebuilt Unicode and CJK fonts

TFT_eSPI `.vlw` smooth fonts derived from **DejaVu Sans Bold**, shared
by the xkcd, weather, and photo viewers to render non-ASCII characters
(e.g. `München`, `São Paulo`, `forté`, em dashes, curly quotes).
The Games EPUB reader also uses **Noto Sans CJK SC Bold** for common Chinese,
Japanese, and Korean text.

## What's here

- `sans_bold_<N>.vlw` for every integer size from **12 to 48 px**
  (~65 MB total). Each firmware selects a subset per panel; the full
  set is included so future firmware tweaks don't require regenerating
  the card.
- `epub_cjk_16.vlw` and `epub_cjk_24.vlw` — complete Basic Multilingual
  Plane fonts for EPUB content and readable SD filenames (~12 MB and
  ~24.5 MB, 42,220 glyphs each).
- `LICENSE.dejavu` — the Bitstream Vera Fonts License, which covers
  redistribution of the `.vlw` derivatives.
- `LICENSE.noto` — the SIL Open Font License 1.1 for the CJK derivative.

## Installing on the device

Copy the whole folder to the root of the microSD card so the files
land at `/fonts/sans_bold_<size>.vlw`. The firmware auto-detects them
at boot and logs `[font] loaded sans_bold_<N> in <ms> ms`. If a file
is missing, the firmware falls back to the built-in GFX FreeFonts —
text still renders but any non-ASCII bytes are dropped.

Two convenient ways to get them onto the card:

- **From this repository.** After cloning, copy `fonts/*.vlw` (and
  optionally `LICENSE.dejavu`) into `/fonts/` on the SD card.
- **From a GitHub release.** Every tagged release attaches a complete
  `fonts.zip` bundle to the release page. Download it from
  <https://github.com/danpodeanu/seeed-reterminal-E100X/releases>,
  then unzip it at the SD-card root. The archive already contains the
  top-level `fonts` folder.

## Regenerating from source

The generator lives at `tools/fonts/make_vlw.py` (needs Pillow +
fontTools):

```bash
python tools/fonts/make_vlw.py fonts/
```

Defaults to `tools/fonts/DejaVuSans-Bold.ttf` and every size 12–48.
Pass `--ttf <path>` to use a different face, or `--size <n>`
(repeatable) to restrict the set.

The CJK font has a separate reproducible generator documented in
[`tools/fonts/README.md`](../tools/fonts/README.md).

## License

DejaVu Sans is distributed under the Bitstream Vera Fonts License.
Noto Sans CJK is distributed under the SIL Open Font License 1.1.
See `LICENSE.dejavu` and `LICENSE.noto`; both must accompany redistribution
of their corresponding `.vlw` derivatives.
