# epdoptimize (bundled)

This directory documents the bundled copy of
[paperlesspaper/epdoptimize](https://github.com/paperlesspaper/epdoptimize).

- **Upstream**: https://github.com/paperlesspaper/epdoptimize
- **Version bundled**: 1.3.0 (pinned in `tools/embed_epdoptimize.py`)
- **License**: Apache-2.0 — full text is preserved in `LICENSE` next to this
  file.
- **Modifications**: none. The upstream ESM bundle
  (`dist/index.mjs` from jsDelivr) is gzipped verbatim and embedded as a
  `PROGMEM` byte array in `common-sd-web/src/epdoptimize_js.cpp`. The
  firmware serves the exact same bytes at `/epdoptimize.mjs` with
  `Content-Encoding: gzip`.

To refresh the bundled copy after an upstream release, run:

```sh
python tools/embed_epdoptimize.py --version <new-version>
```

and update the version number in this README.
