# Font sources and generators

`make_vlw.py` converts TTF/OTF fonts to the smooth VLW format supported by
TFT_eSPI and Seeed_GFX. The prebuilt SD-card fonts use the bundled DejaVu Sans
Bold source.

The Games interface embeds a small multilingual glyph subset generated from
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
python games/tools/generate_ui_fonts.py
```

The source font is intentionally not checked in. The generated
`games/src/game_ui_fonts.h` contains only the ASCII and localized UI glyphs at
16, 24, and 32 pixels. Noto Sans CJK is licensed under the SIL Open Font
License 1.1; see `LICENSE.noto`.
