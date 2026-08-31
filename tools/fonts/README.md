# Font sources and generators

`make_vlw.py` converts TTF/OTF fonts to the smooth VLW format supported by
TFT_eSPI and Seeed_GFX. The prebuilt SD-card fonts use the bundled DejaVu Sans
Bold source.

Calendar Viewer uses **Noto Sans SemiCondensed Bold** for all on-panel text. It
embeds one-bit UI fonts at 10, 16, 18, 24, 36, and 48 pixels, plus sparse 18px
and 24px Latin-script fonts for event titles. To regenerate them, download this
pinned source:

```text
https://raw.githubusercontent.com/notofonts/noto-fonts/ffebf8c1ee449e544955a7e813c54f9b73848eac/hinted/ttf/NotoSans/NotoSans-SemiCondensedBold.ttf
```

Its SHA-256 is
`f7dc6bbca7a6b134600a49ca05352cb51cb519305b554bd93311bcd2c925d1bc`.
Save it as `tools/fonts/NotoSans-SemiCondensedBold.ttf`, then run:

```bash
python calendar-viewer/tools/generate_latin_font.py
```

The source TTF is intentionally not checked in. The generated header contains
compact ASCII UI fonts, UI Latin fonts through U+024F at 18px and 24px, and
broad event-title subsets covering Latin, Latin Extended, phonetic and
combining forms, punctuation, and currency glyphs. Greek, Cyrillic, Thai, CJK,
and other scripts are not embedded.

The Sticky Arcade interface embeds a small multilingual glyph subset generated from
**Noto Sans CJK SC Bold**. To regenerate it, download this pinned source:

```text
https://raw.githubusercontent.com/notofonts/noto-cjk/f8d157532fbfaeda587e826d4cd5b21a49186f7c/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Bold.otf
```

Save it as `tools/fonts/NotoSansCJKsc-Bold.otf` and verify its SHA-256:

```text
B5F0D1A190A7F9B43C310A8850630AF12553DF32C4C050543F9059732D9B4C0A
```

Then run:

```bash
python sticky-arcade/tools/generate_ui_fonts.py
```

The source font is intentionally not checked in. The generated
`sticky-arcade/src/game_ui_fonts.h` contains the ASCII and localized UI glyphs at 16,
24, and 32 pixels, plus the localized help-text glyphs at 24 pixels. Noto Sans
CJK is licensed under the SIL Open Font License 1.1; see `LICENSE.noto`.

## EPUB styled Latin fonts

The EPUB reader embeds 24px Noto Serif regular, bold, italic, and bold-italic
subsets for Latin, Latin Extended, combining marks, punctuation, and currency
symbols. The generator quantizes antialiased coverage at 25% so Seeed's one-bit
sprite preserves glyph edges instead of dropping every partially covered pixel.
Download these four files from the pinned `notofonts/noto-fonts`
commit `ffebf8c1ee449e544955a7e813c54f9b73848eac`:

```text
hinted/ttf/NotoSerif/NotoSerif-Regular.ttf
hinted/ttf/NotoSerif/NotoSerif-Bold.ttf
hinted/ttf/NotoSerif/NotoSerif-Italic.ttf
hinted/ttf/NotoSerif/NotoSerif-BoldItalic.ttf
```

Place them in one directory, then run:

```bash
python sticky-arcade/tools/generate_epub_latin_fonts.py /path/to/noto-serif
```

The generator verifies all four pinned SHA-256 hashes and rewrites
`sticky-arcade/src/epub_latin_fonts.h`. The source TTF files are intentionally not
checked in.

## EPUB CJK font

The Sticky Arcade EPUB reader can load the same Noto source from the SD card as
complete 16px and 24px Basic Multilingual Plane fonts. The generated fonts are
committed under `fonts/` and included in the `fonts.zip` attached to every tagged
release. To regenerate it after downloading and verifying the source above,
write it directly onto a mounted SD card:

```bash
python sticky-arcade/tools/generate_epub_cjk_font.py /path/to/sd-card
```

On Windows, use the drive root, for example
`python sticky-arcade\tools\generate_epub_cjk_font.py E:\`. The script verifies the
pinned source SHA-256 and writes `fonts/epub_cjk_16.vlw` and
`fonts/epub_cjk_24.vlw` beneath the supplied root. They are approximately
12 MB and 24.5 MB with 42,220 glyphs each. Only the generated VLW files and
their `LICENSE.noto` belong on the SD card.
