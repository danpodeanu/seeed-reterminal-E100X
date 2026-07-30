// Browser-side photo uploader.
//
// Served at /upload-photo when the embedding app supplies panel
// dimensions + palette in sd_web_portal::Config. The page fetches
// /panel.json to learn what the target panel looks like, then:
//
//   1. Waits for the user to pick a photo (input file).
//   2. Decodes the file into an ImageBitmap. Browsers apply EXIF
//      orientation for free when `imageOrientation: 'from-image'` is
//      passed, so an upright iPhone portrait stays upright.
//   3. Cover-fit-crops + rescales to the panel size using canvas
//      drawImage with high-quality resampling.
//   4. Runs Floyd-Steinberg dithering (or optional None) in JS,
//      quantising to the panel's palette:
//        - gray4 : E1001, 4 gray levels
//        - gray16: E1003, 16 gray levels
//        - e6    : E1002 / E1004, 6-colour Spectra
//   5. Encodes the dithered result as a 4-bit BMP - the same shape
//      photo-viewer's fast path expects, so no on-device dithering is
//      needed.
//   6. Uploads via POST to /upload?parent=<photosDir>, reusing the
//      existing multipart streaming handler.
//
// Kept self-contained: no external JS, no CSS framework, no build step.
// The whole page is one PROGMEM string so it lives in flash and
// serialises to the client via WebServer::send_P.

#include <Arduino.h>

namespace sd_web_portal {

extern const char kPhotoUploadPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>Upload photo</title>
<style>
:root {
  --bg: #f4f6f8;
  --card: #ffffff;
  --ink: #1f2937;
  --muted: #6b7280;
  --line: #e5e7eb;
  --accent: #2563eb;
  --accent-ink: #ffffff;
}
* { box-sizing: border-box; }
html, body { margin: 0; padding: 0; background: var(--bg); color: var(--ink);
  font: 16px/1.5 -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto,
        Helvetica, Arial, sans-serif; }
header { padding: 16px 20px; background: linear-gradient(180deg, #1f2937, #111827);
  color: #f9fafb; }
header h1 { margin: 0; font-size: 1.15rem; font-weight: 600; }
header .sub { margin-top: 4px; font-size: 0.85rem; color: #cbd5e1; }
main { padding: 16px; max-width: 720px; margin: 0 auto; }
.card { background: var(--card); border: 1px solid var(--line);
  border-radius: 10px; padding: 16px; margin-bottom: 16px; }
.card h2 { margin: 0 0 8px 0; font-size: 1rem; font-weight: 600; }
.card p.hint { margin: 0 0 12px 0; color: var(--muted); font-size: 0.9rem; }
label.file {
  display: block; padding: 14px; text-align: center;
  border: 2px dashed var(--line); border-radius: 8px;
  color: var(--muted); background: #fafafa; cursor: pointer;
}
label.file input { display: none; }
label.file.have { border-style: solid; border-color: var(--accent);
  background: #eff6ff; color: var(--ink); }
.row { display: flex; gap: 12px; align-items: center; flex-wrap: wrap; }
.row label { color: var(--muted); font-size: 0.9rem; }
select, button {
  font: inherit; padding: 10px 14px; border-radius: 8px; border: 1px solid var(--line);
  background: var(--card); color: var(--ink);
}
button.primary { background: var(--accent); color: var(--accent-ink); border-color: var(--accent);
  font-weight: 600; }
button:disabled { opacity: 0.5; cursor: not-allowed; }
canvas {
  display: block; margin: 0 auto; max-width: 100%; height: auto;
  background: #ffffff; border: 1px solid var(--line); border-radius: 6px;
  image-rendering: pixelated;
}
.status { min-height: 1.5em; font-size: 0.9rem; color: var(--muted); }
.status.err { color: #dc2626; }
.status.ok { color: #059669; }
footer { text-align: center; color: var(--muted); font-size: 0.8rem;
  padding: 12px 20px 24px; }
footer a { color: var(--muted); }
</style>
</head>
<body>
<header>
  <h1>Upload photo</h1>
  <div class="sub" id="panelLabel">Loading panel...</div>
</header>
<main>
  <div class="card">
    <h2>Pick a photo</h2>
    <p class="hint">Anything your phone can share: JPG, HEIC (Safari converts), PNG.</p>
    <label class="file" id="pickerLabel">
      <span id="pickerText">Tap to choose photo</span>
      <input type="file" id="picker" accept="image/*">
    </label>
  </div>

  <div class="card" id="previewCard" hidden>
    <h2>Preview</h2>
    <p class="hint">This is what the panel will show.</p>
    <div class="row" style="justify-content: space-between; margin-bottom: 10px;">
      <label>Dither
        <select id="dither">
          <option value="fs" selected>Floyd-Steinberg (photos)</option>
          <option value="none">None (posterise)</option>
        </select>
      </label>
      <span class="status" id="status"></span>
    </div>
    <canvas id="preview"></canvas>
    <div class="row" style="justify-content: flex-end; margin-top: 12px;">
      <button id="upload" class="primary" disabled>Upload to panel</button>
    </div>
  </div>

  <div class="card">
    <p class="hint" style="margin:0">Uploaded photos appear on the panel on the next refresh.
      Use the panel's arrow buttons to leave this page.</p>
    <p class="hint" style="margin:6px 0 0 0"><a href="/browse?path=%2F">Back to file browser</a></p>
  </div>
</main>
<footer>reTerminal photo portal</footer>

<script>
"use strict";

const PALETTES = {
  gray4: [
    [0, 0, 0], [85, 85, 85], [170, 170, 170], [255, 255, 255],
  ],
  gray16: (() => {
    const out = [];
    for (let i = 0; i < 16; i++) { const v = i * 17; out.push([v, v, v]); }
    return out;
  })(),
  e6: [
    [255, 255, 255], // white
    [29, 185, 84],   // green
    [229, 57, 53],   // red
    [255, 216, 0],   // yellow
    [0, 76, 255],    // blue
    [0, 0, 0],       // black
  ],
};

const state = {
  panel: null,       // { width, height, palette, model, photosDir }
  sourceRgba: null,  // Uint8ClampedArray, resized to panel size, RGBA
  file: null,
  fileBase: "photo",
};

const $ = (id) => document.getElementById(id);

async function fetchPanel() {
  try {
    const r = await fetch("/panel.json", { cache: "no-store" });
    if (!r.ok) throw new Error("panel.json " + r.status);
    state.panel = await r.json();
  } catch (e) {
    setStatus("Cannot load panel info: " + e.message, "err");
    return;
  }
  const p = state.panel;
  const paletteLabel = ({ gray4: "4 gray levels", gray16: "16 gray levels",
    e6: "6 colors" })[p.palette] || p.palette;
  $("panelLabel").textContent = `${p.model || "Panel"}: ${p.width} x ${p.height}, ${paletteLabel}`;
}

function setStatus(text, kind) {
  const el = $("status");
  el.textContent = text || "";
  el.className = "status" + (kind ? " " + kind : "");
}

function coverCrop(srcW, srcH, dstW, dstH) {
  const srcAsp = srcW / srcH, dstAsp = dstW / dstH;
  let sw, sh, sx, sy;
  if (srcAsp > dstAsp) {
    sh = srcH; sw = Math.round(sh * dstAsp);
    sx = Math.round((srcW - sw) / 2); sy = 0;
  } else {
    sw = srcW; sh = Math.round(sw / dstAsp);
    sx = 0; sy = Math.round((srcH - sh) / 2);
  }
  return { sx, sy, sw, sh };
}

async function decodeAndRescale(file) {
  const bmp = await createImageBitmap(file, { imageOrientation: "from-image" });
  const { width: pw, height: ph } = state.panel;
  const { sx, sy, sw, sh } = coverCrop(bmp.width, bmp.height, pw, ph);
  const cv = document.createElement("canvas");
  cv.width = pw; cv.height = ph;
  const ctx = cv.getContext("2d", { willReadFrequently: true });
  ctx.imageSmoothingEnabled = true;
  ctx.imageSmoothingQuality = "high";
  ctx.drawImage(bmp, sx, sy, sw, sh, 0, 0, pw, ph);
  bmp.close();
  return ctx.getImageData(0, 0, pw, ph);
}

// Floyd-Steinberg into a palette. `imageData` is RGBA at panel size.
// Returns Uint8Array of palette indices (one per pixel), plus a
// dithered RGBA buffer for preview.
function dither(imageData, palette, method) {
  const w = imageData.width, h = imageData.height;
  const src = imageData.data;                    // RGBA
  const work = new Float32Array(w * h * 3);      // RGB working buffer
  for (let i = 0, j = 0; i < src.length; i += 4, j += 3) {
    work[j] = src[i]; work[j + 1] = src[i + 1]; work[j + 2] = src[i + 2];
  }
  const indices = new Uint8Array(w * h);
  const preview = new Uint8ClampedArray(w * h * 4);
  const pr = new Float32Array(palette.length);
  const pg = new Float32Array(palette.length);
  const pb = new Float32Array(palette.length);
  for (let p = 0; p < palette.length; p++) {
    pr[p] = palette[p][0]; pg[p] = palette[p][1]; pb[p] = palette[p][2];
  }
  const fs = (method === "fs");
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const i = (y * w + x) * 3;
      const r = work[i], g = work[i + 1], b = work[i + 2];
      let best = 0, bestD = Infinity;
      for (let p = 0; p < palette.length; p++) {
        const dr = r - pr[p], dg = g - pg[p], db = b - pb[p];
        const d = dr * dr + dg * dg + db * db;
        if (d < bestD) { bestD = d; best = p; }
      }
      indices[y * w + x] = best;
      const cr = pr[best], cg = pg[best], cb = pb[best];
      const oi = (y * w + x) * 4;
      preview[oi] = cr; preview[oi + 1] = cg; preview[oi + 2] = cb; preview[oi + 3] = 255;
      if (fs) {
        const er = r - cr, eg = g - cg, eb = b - cb;
        if (x + 1 < w) {
          const j = i + 3;
          work[j]     += er * (7 / 16);
          work[j + 1] += eg * (7 / 16);
          work[j + 2] += eb * (7 / 16);
        }
        if (y + 1 < h) {
          if (x > 0) {
            const j = i + (w - 1) * 3;
            work[j]     += er * (3 / 16);
            work[j + 1] += eg * (3 / 16);
            work[j + 2] += eb * (3 / 16);
          }
          const j2 = i + w * 3;
          work[j2]     += er * (5 / 16);
          work[j2 + 1] += eg * (5 / 16);
          work[j2 + 2] += eb * (5 / 16);
          if (x + 1 < w) {
            const j3 = i + (w + 1) * 3;
            work[j3]     += er * (1 / 16);
            work[j3 + 1] += eg * (1 / 16);
            work[j3 + 2] += eb * (1 / 16);
          }
        }
      }
    }
  }
  return { indices, preview };
}

// 4-bit BMP writer. Layout matches photo-viewer's fast path:
//   BITMAPFILEHEADER (14) + BITMAPINFOHEADER (40) + 16*RGBQUAD palette
//   + rows bottom-up, 4-byte aligned.
function encode4bitBmp(indices, width, height, palette) {
  const packedRow = (width + 1) >> 1;
  const stride = (packedRow + 3) & ~3;
  const pixelBytes = stride * height;
  const paletteBytes = 16 * 4;
  const pixelOffset = 14 + 40 + paletteBytes;
  const fileSize = pixelOffset + pixelBytes;
  const buf = new ArrayBuffer(fileSize);
  const dv = new DataView(buf);
  // BITMAPFILEHEADER
  dv.setUint8(0, 0x42); dv.setUint8(1, 0x4D);   // "BM"
  dv.setUint32(2, fileSize, true);
  dv.setUint32(10, pixelOffset, true);
  // BITMAPINFOHEADER
  dv.setUint32(14, 40, true);
  dv.setInt32(18, width, true);
  dv.setInt32(22, height, true);
  dv.setUint16(26, 1, true);
  dv.setUint16(28, 4, true);
  dv.setUint32(30, 0, true);              // BI_RGB
  dv.setUint32(34, pixelBytes, true);
  dv.setInt32(38, 2835, true);            // 72 dpi
  dv.setInt32(42, 2835, true);
  dv.setUint32(46, 16, true);
  dv.setUint32(50, palette.length, true);
  // Palette (BGRA), pad unused slots by repeating the last colour.
  for (let p = 0; p < 16; p++) {
    const [r, g, b] = palette[p < palette.length ? p : palette.length - 1];
    const off = 54 + p * 4;
    dv.setUint8(off,     b);
    dv.setUint8(off + 1, g);
    dv.setUint8(off + 2, r);
    dv.setUint8(off + 3, 0);
  }
  // Pixel data, bottom-up.
  const pixels = new Uint8Array(buf, pixelOffset, pixelBytes);
  for (let y = 0; y < height; y++) {
    const srcRow = (height - 1 - y) * width;
    const dstRow = y * stride;
    for (let x = 0; x < width; x += 2) {
      const left = indices[srcRow + x] & 0x0F;
      const right = (x + 1 < width) ? (indices[srcRow + x + 1] & 0x0F) : 0;
      pixels[dstRow + (x >> 1)] = (left << 4) | right;
    }
  }
  return new Blob([buf], { type: "image/bmp" });
}

function stemFor(name) {
  const base = (name || "photo").split(/[\\/]/).pop();
  const dot = base.lastIndexOf(".");
  const stem = (dot > 0 ? base.slice(0, dot) : base) || "photo";
  return stem.replace(/[^A-Za-z0-9._-]+/g, "_").slice(0, 40) || "photo";
}

async function runPipeline() {
  const method = $("dither").value;
  const palette = PALETTES[state.panel.palette] || PALETTES.gray4;
  setStatus("Preparing preview...");
  // Give the browser a paint tick so the status shows.
  await new Promise(r => setTimeout(r, 20));
  const t0 = performance.now();
  const { indices, preview } = dither(state.sourceRgba, palette, method);
  const t1 = performance.now();
  const cv = $("preview");
  cv.width = state.panel.width; cv.height = state.panel.height;
  const ctx = cv.getContext("2d");
  ctx.putImageData(new ImageData(preview, cv.width, cv.height), 0, 0);
  state.currentIndices = indices;
  state.currentPalette = palette;
  setStatus(`Ready (dither ${Math.round(t1 - t0)} ms).`, "ok");
  $("upload").disabled = false;
}

async function onPick(e) {
  const file = e.target.files && e.target.files[0];
  if (!file) return;
  state.file = file;
  state.fileBase = stemFor(file.name);
  $("pickerLabel").classList.add("have");
  $("pickerText").textContent = file.name;
  $("previewCard").hidden = false;
  $("upload").disabled = true;
  setStatus("Decoding...");
  try {
    state.sourceRgba = await decodeAndRescale(file);
  } catch (err) {
    setStatus("Could not decode this file: " + err.message, "err");
    return;
  }
  await runPipeline();
}

async function onUpload() {
  if (!state.currentIndices) return;
  $("upload").disabled = true;
  setStatus("Encoding BMP...");
  const bmp = encode4bitBmp(state.currentIndices, state.panel.width,
                            state.panel.height, state.currentPalette);
  const fd = new FormData();
  fd.append("file", bmp, state.fileBase + ".bmp");
  const parent = state.panel.photosDir || "/photos";
  setStatus(`Uploading ${Math.round(bmp.size / 1024)} KB...`);
  try {
    const res = await fetch("/upload?parent=" + encodeURIComponent(parent), {
      method: "POST",
      body: fd,
    });
    if (!res.ok) throw new Error("HTTP " + res.status);
    setStatus("Uploaded. Next panel refresh will show this photo.", "ok");
  } catch (err) {
    setStatus("Upload failed: " + err.message, "err");
    $("upload").disabled = false;
  }
}

$("picker").addEventListener("change", onPick);
$("dither").addEventListener("change", () => {
  if (state.sourceRgba) runPipeline();
});
$("upload").addEventListener("click", onUpload);

fetchPanel();
</script>
</body>
</html>
)HTML";

}  // namespace sd_web_portal
