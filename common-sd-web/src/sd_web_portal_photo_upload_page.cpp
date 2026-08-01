// Browser-side photo uploader.
//
// Served at /upload-photo when the embedding app supplies panel
// dimensions + palette in sd_web_portal::Config. The page fetches
// /photo-panel.json to learn what the target panel looks like, then:
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

extern const char kPhotoUploadPageHead[] PROGMEM = R"HTML(<!doctype html>
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
.stage {
  position: relative; margin: 0 auto; touch-action: none;
  max-width: 100%; user-select: none; -webkit-user-select: none;
}
.stage canvas.crop {
  image-rendering: auto; cursor: grab;
}
.stage canvas.crop.dragging { cursor: grabbing; }
.pan-hint {
  text-align: center; color: var(--muted); font-size: 0.85rem;
  margin: 8px 0 0 0;
}
.status { min-height: 1.5em; font-size: 0.9rem; color: var(--muted); }
.status.err { color: #dc2626; }
.status.ok { color: #059669; }
footer { text-align: center; color: var(--muted); font-size: 0.8rem;
  padding: 12px 20px 24px; }
footer a { color: var(--muted); }
.photo-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(120px, 1fr));
  gap: 10px;
}
.photo-tile {
  border: 1px solid var(--line); border-radius: 8px; overflow: hidden;
  background: #fafafa; display: flex; flex-direction: column;
}
.photo-tile img {
  width: 100%; aspect-ratio: 1 / 1; object-fit: cover; display: block;
  background: #fff;
}
.photo-tile .meta {
  padding: 6px 8px; font-size: 0.75rem; color: var(--muted);
  border-top: 1px solid var(--line); display: flex;
  justify-content: space-between; align-items: center; gap: 6px;
}
.photo-tile .meta .name {
  overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
  color: var(--ink);
}
.photo-tile button.del {
  padding: 3px 8px; font-size: 0.75rem; border-radius: 6px;
  border: 1px solid #dc2626; background: #fff; color: #dc2626;
  cursor: pointer;
}
.photo-tile button.del:hover { background: #fef2f2; }
.photo-tile button.del:disabled { opacity: 0.5; cursor: not-allowed; }
</style>
</head>
<body>
)HTML";

extern const char kPhotoUploadPageTail[] PROGMEM = R"HTML(<header>
  <h1>Upload photo</h1>
  <div class="sub" id="panelLabel">Loading panel...</div>
</header>
<main>
  <div class="card" id="uploadedBanner" hidden>
    <h2 style="color:#059669;margin:0 0 6px 0;">Uploaded</h2>
    <p class="hint" style="margin:0">Next panel refresh will show this photo. Pick another photo below to upload more.</p>
  </div>

  <div class="card">
    <h2>Pick a photo</h2>
    <p class="hint">JPG or PNG.</p>
    <label class="file" id="pickerLabel">
      <span id="pickerText">Tap to choose photo</span>
      <input type="file" id="picker" accept="image/jpeg,image/png">
    </label>
  </div>

  <div class="card" id="cropCard" hidden>
    <h2>Crop &amp; pan</h2>
    <p class="hint">Drag the photo inside the frame to choose what the panel shows.</p>
    <div class="stage" id="cropStage">
      <canvas class="crop" id="crop"></canvas>
    </div>
    <p class="pan-hint" id="cropHint">Drag to pan</p>
  </div>

  <div class="card" id="previewCard" hidden>
    <h2>Preview</h2>
    <p class="hint">This is what the panel will show.</p>
    <div class="row" style="justify-content: space-between; margin-bottom: 10px;">
      <label>Dither
        <select id="dither">
          <option value="fs" selected>Floyd-Steinberg (photos)</option>
          <option value="atkinson">Atkinson (soft, e-paper favourite)</option>
          <option value="jjn">Jarvis-Judice-Ninke (rich detail)</option>
          <option value="stucki">Stucki (sharper JJN)</option>
          <option value="sierra">Sierra Lite (fast)</option>
          <option value="bayer8">Ordered 8x8 (crosshatch)</option>
          <option value="bayer4">Ordered 4x4 (coarser)</option>
          <option value="none">None (posterise)</option>
        </select>
      </label>
      <span class="status" id="status"></span>
    </div>
    <div class="row" style="gap: 10px; margin-bottom: 10px;">
      <label style="flex: 1; display: flex; align-items: center; gap: 8px;">
        <span>Brightness</span>
        <input type="range" id="gamma" min="0.5" max="2.0" step="0.05"
               value="1.0" style="flex: 1;">
        <span id="gammaValue" style="min-width: 3ch; text-align: right;
              font-variant-numeric: tabular-nums;">1.00</span>
        <button type="button" id="gammaReset" style="padding: 4px 10px;
                font-size: 0.85rem;">Reset</button>
      </label>
    </div>
    <canvas id="preview"></canvas>
    <div class="row" style="justify-content: flex-end; margin-top: 12px;">
      <button id="upload" class="primary" disabled>Upload to panel</button>
    </div>
  </div>

  <div class="card">
    <h2>Your photos</h2>
    <p class="hint" style="margin:0 0 10px 0" id="photosStatus">Loading...</p>
    <div id="photoGrid" class="photo-grid"></div>
  </div>

  <div class="card">
    <h2>Reboot to viewer</h2>
    <p class="hint" style="margin:0 0 10px 0">Restart the panel back into photo-viewer mode. You can also press the left or right arrow on the device.</p>
    <div class="row" style="justify-content: flex-end;">
      <button type="button" id="rebootBtn" class="primary">Reboot to viewer</button>
    </div>
  </div>

  <div class="card">
    <p class="hint" style="margin:0">Uploaded photos appear on the panel on the next refresh.
      Use the panel's arrow buttons to leave this page.</p>
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
  bitmap: null,      // ImageBitmap, EXIF-oriented source
  crop: null,        // { sx, sy, sw, sh } in source pixels (panel aspect)
  displayScale: 1,   // source-px per display-canvas-px
  sourceRgba: null,  // ImageData at panel size, RGBA (cropped)
  file: null,
  fileBase: "photo",
  currentIndices: null,
  currentPalette: null,
};

const $ = (id) => document.getElementById(id);

async function fetchPanel() {
  try {
    const r = await fetch("/photo-panel.json", { cache: "no-store" });
    if (!r.ok) throw new Error("photo-panel.json " + r.status);
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

// Cover-fit: the largest panel-aspect rectangle that fits inside the
// source. Returns {sw, sh} plus the maximum sx/sy the caller can pan to.
function coverRect(srcW, srcH, dstW, dstH) {
  const srcAsp = srcW / srcH, dstAsp = dstW / dstH;
  let sw, sh;
  if (srcAsp > dstAsp) { sh = srcH; sw = Math.round(sh * dstAsp); }
  else                 { sw = srcW; sh = Math.round(sw / dstAsp); }
  return { sw, sh, maxSx: srcW - sw, maxSy: srcH - sh };
}

function clampCrop() {
  const c = state.crop;
  if (!c) return;
  c.sx = Math.max(0, Math.min(c.sx, state.bitmap.width - c.sw));
  c.sy = Math.max(0, Math.min(c.sy, state.bitmap.height - c.sh));
}

// Redraws the crop stage: the source scaled to fit the display box, with
// a dim overlay outside the panel-aspect crop rectangle.
function drawCropStage() {
  const cv = $("crop");
  const bm = state.bitmap;
  if (!bm) return;
  // Fit the source into a display box sized to the card width, capped
  // vertically so the panel-portrait side does not blow up.
  const stage = $("cropStage");
  const boxW = Math.max(240, Math.min(stage.clientWidth || 640, 640));
  const boxH = 360;
  const scale = Math.min(boxW / bm.width, boxH / bm.height);
  const dispW = Math.max(1, Math.round(bm.width * scale));
  const dispH = Math.max(1, Math.round(bm.height * scale));
  cv.width = dispW; cv.height = dispH;
  cv.style.width = dispW + "px"; cv.style.height = dispH + "px";
  state.displayScale = 1 / scale;

  const ctx = cv.getContext("2d");
  ctx.imageSmoothingEnabled = true;
  ctx.imageSmoothingQuality = "medium";
  ctx.clearRect(0, 0, dispW, dispH);
  ctx.drawImage(bm, 0, 0, dispW, dispH);

  clampCrop();
  const c = state.crop;
  const rx = c.sx * scale, ry = c.sy * scale;
  const rw = c.sw * scale, rh = c.sh * scale;
  // Dim outside the rect.
  ctx.fillStyle = "rgba(0,0,0,0.45)";
  ctx.fillRect(0, 0, dispW, ry);
  ctx.fillRect(0, ry + rh, dispW, dispH - (ry + rh));
  ctx.fillRect(0, ry, rx, rh);
  ctx.fillRect(rx + rw, ry, dispW - (rx + rw), rh);
  // Rect border.
  ctx.lineWidth = 2;
  ctx.strokeStyle = "#ffffff";
  ctx.strokeRect(rx + 1, ry + 1, rw - 2, rh - 2);
  ctx.strokeStyle = "#111827";
  ctx.strokeRect(rx, ry, rw, rh);

  const hint = $("cropHint");
  const rectC = coverRect(bm.width, bm.height, state.panel.width, state.panel.height);
  if (rectC.maxSx === 0 && rectC.maxSy === 0)
    hint.textContent = "Photo already matches the panel aspect ratio.";
  else if (rectC.maxSx === 0)
    hint.textContent = "Drag up or down to choose the vertical crop.";
  else if (rectC.maxSy === 0)
    hint.textContent = "Drag left or right to choose the horizontal crop.";
  else
    hint.textContent = "Drag to pan.";
}

function bindCropDrag() {
  const cv = $("crop");
  let dragging = false, startX = 0, startY = 0, startSx = 0, startSy = 0;
  cv.addEventListener("pointerdown", (e) => {
    if (!state.bitmap) return;
    dragging = true;
    cv.classList.add("dragging");
    cv.setPointerCapture(e.pointerId);
    startX = e.clientX; startY = e.clientY;
    startSx = state.crop.sx; startSy = state.crop.sy;
  });
  cv.addEventListener("pointermove", (e) => {
    if (!dragging) return;
    const dx = (e.clientX - startX) * state.displayScale;
    const dy = (e.clientY - startY) * state.displayScale;
    state.crop.sx = startSx - dx;
    state.crop.sy = startSy - dy;
    drawCropStage();
  });
  const finish = (e) => {
    if (!dragging) return;
    dragging = false;
    cv.classList.remove("dragging");
    try { cv.releasePointerCapture(e.pointerId); } catch (_) {}
    runPipeline();
  };
  cv.addEventListener("pointerup", finish);
  cv.addEventListener("pointercancel", finish);
}

// Take the current crop from state.bitmap into a panel-sized ImageData.
function rescaleFromCrop() {
  const { width: pw, height: ph } = state.panel;
  const c = state.crop;
  const cv = document.createElement("canvas");
  cv.width = pw; cv.height = ph;
  const ctx = cv.getContext("2d", { willReadFrequently: true });
  ctx.imageSmoothingEnabled = true;
  ctx.imageSmoothingQuality = "high";
  ctx.drawImage(state.bitmap, c.sx, c.sy, c.sw, c.sh, 0, 0, pw, ph);
  return ctx.getImageData(0, 0, pw, ph);
}

// Gamma-correct RGB in place. gamma > 1 lifts mid-tones (brighter);
// gamma < 1 crushes them (darker). Uses a 256-entry lookup table so the
// per-pixel cost is a table read, not a pow() call. Skipped when gamma
// is close to 1 to keep the identity case free.
function applyGamma(imageData, gamma) {
  if (Math.abs(gamma - 1) < 0.005) return;
  const inv = 1 / gamma;
  const lut = new Uint8ClampedArray(256);
  for (let i = 0; i < 256; i++) {
    lut[i] = Math.round(255 * Math.pow(i / 255, inv));
  }
  const d = imageData.data;
  for (let i = 0; i < d.length; i += 4) {
    d[i]     = lut[d[i]];
    d[i + 1] = lut[d[i + 1]];
    d[i + 2] = lut[d[i + 2]];
  }
}

// Error-diffusion kernels. Each entry is [dx, dy, weight]. Weights are
// pre-normalised (each row sums to 1 after dividing by the divisor). We
// only emit forward-only entries because the raster order is L->R,T->B.
const KERNELS = {
  fs: {  // Floyd-Steinberg, 1957
    div: 16,
    taps: [[ 1, 0, 7], [-1, 1, 3], [ 0, 1, 5], [ 1, 1, 1]],
  },
  atkinson: {  // Bill Atkinson, 1984. Only 6/8 of the error is diffused;
               // slightly desaturates but stays crisp on e-paper.
    div: 8,
    taps: [[ 1, 0, 1], [ 2, 0, 1],
           [-1, 1, 1], [ 0, 1, 1], [ 1, 1, 1],
           [ 0, 2, 1]],
  },
  jjn: {  // Jarvis, Judice, Ninke, 1976. Large kernel, smooth photos.
    div: 48,
    taps: [[ 1, 0, 7], [ 2, 0, 5],
           [-2, 1, 3], [-1, 1, 5], [ 0, 1, 7], [ 1, 1, 5], [ 2, 1, 3],
           [-2, 2, 1], [-1, 2, 3], [ 0, 2, 5], [ 1, 2, 3], [ 2, 2, 1]],
  },
  stucki: {  // Peter Stucki, 1981. Sharper JJN.
    div: 42,
    taps: [[ 1, 0, 8], [ 2, 0, 4],
           [-2, 1, 2], [-1, 1, 4], [ 0, 1, 8], [ 1, 1, 4], [ 2, 1, 2],
           [-2, 2, 1], [-1, 2, 2], [ 0, 2, 4], [ 1, 2, 2], [ 2, 2, 1]],
  },
  sierra: {  // Sierra Lite - cheap two-row kernel.
    div: 4,
    taps: [[ 1, 0, 2], [-1, 1, 1], [ 0, 1, 1]],
  },
};

// Bayer matrices (0..N^2-1). Threshold t is (m + 0.5) / N^2 - 0.5, added
// to the pixel value scaled by a bias so it nudges the nearest-color pick.
const BAYER = {
  bayer4: {
    n: 4,
    m: [
       0,  8,  2, 10,
      12,  4, 14,  6,
       3, 11,  1,  9,
      15,  7, 13,  5,
    ],
  },
  bayer8: {
    n: 8,
    m: [
       0, 32,  8, 40,  2, 34, 10, 42,
      48, 16, 56, 24, 50, 18, 58, 26,
      12, 44,  4, 36, 14, 46,  6, 38,
      60, 28, 52, 20, 62, 30, 54, 22,
       3, 35, 11, 43,  1, 33,  9, 41,
      51, 19, 59, 27, 49, 17, 57, 25,
      15, 47,  7, 39, 13, 45,  5, 37,
      63, 31, 55, 23, 61, 29, 53, 21,
    ],
  },
};

function nearestPaletteIndex(r, g, b, pr, pg, pb) {
  let best = 0, bestD = Infinity;
  for (let p = 0; p < pr.length; p++) {
    const dr = r - pr[p], dg = g - pg[p], db = b - pb[p];
    const d = dr * dr + dg * dg + db * db;
    if (d < bestD) { bestD = d; best = p; }
  }
  return best;
}

function ditherErrorDiffusion(imageData, palette, kernel) {
  const w = imageData.width, h = imageData.height;
  const src = imageData.data;
  const work = new Float32Array(w * h * 3);
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
  const div = kernel.div, taps = kernel.taps;
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const i = (y * w + x) * 3;
      const r = work[i], g = work[i + 1], b = work[i + 2];
      const best = nearestPaletteIndex(r, g, b, pr, pg, pb);
      indices[y * w + x] = best;
      const cr = pr[best], cg = pg[best], cb = pb[best];
      const oi = (y * w + x) * 4;
      preview[oi] = cr; preview[oi + 1] = cg; preview[oi + 2] = cb; preview[oi + 3] = 255;
      const er = r - cr, eg = g - cg, eb = b - cb;
      for (let t = 0; t < taps.length; t++) {
        const dx = taps[t][0], dy = taps[t][1], wt = taps[t][2] / div;
        const nx = x + dx, ny = y + dy;
        if (nx < 0 || nx >= w || ny >= h) continue;
        const j = (ny * w + nx) * 3;
        work[j]     += er * wt;
        work[j + 1] += eg * wt;
        work[j + 2] += eb * wt;
      }
    }
  }
  return { indices, preview };
}

function ditherOrdered(imageData, palette, bayer) {
  const w = imageData.width, h = imageData.height;
  const src = imageData.data;
  const indices = new Uint8Array(w * h);
  const preview = new Uint8ClampedArray(w * h * 4);
  const pr = new Float32Array(palette.length);
  const pg = new Float32Array(palette.length);
  const pb = new Float32Array(palette.length);
  for (let p = 0; p < palette.length; p++) {
    pr[p] = palette[p][0]; pg[p] = palette[p][1]; pb[p] = palette[p][2];
  }
  // Amplitude: the bias we add per pixel. For an N-level gray ramp,
  // one step is 255/(N-1); scale slightly under one step so the pattern
  // shows without clipping.
  const step = 255 / Math.max(1, palette.length - 1);
  const amp = step * 0.9;
  const n = bayer.n, mat = bayer.m, denom = n * n;
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const si = (y * w + x) * 4;
      const bias = ((mat[(y % n) * n + (x % n)] + 0.5) / denom - 0.5) * amp;
      const r = src[si] + bias;
      const g = src[si + 1] + bias;
      const b = src[si + 2] + bias;
      const best = nearestPaletteIndex(r, g, b, pr, pg, pb);
      indices[y * w + x] = best;
      preview[si]     = pr[best];
      preview[si + 1] = pg[best];
      preview[si + 2] = pb[best];
      preview[si + 3] = 255;
    }
  }
  return { indices, preview };
}

function ditherNone(imageData, palette) {
  const w = imageData.width, h = imageData.height;
  const src = imageData.data;
  const indices = new Uint8Array(w * h);
  const preview = new Uint8ClampedArray(w * h * 4);
  const pr = new Float32Array(palette.length);
  const pg = new Float32Array(palette.length);
  const pb = new Float32Array(palette.length);
  for (let p = 0; p < palette.length; p++) {
    pr[p] = palette[p][0]; pg[p] = palette[p][1]; pb[p] = palette[p][2];
  }
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const si = (y * w + x) * 4;
      const best = nearestPaletteIndex(src[si], src[si + 1], src[si + 2], pr, pg, pb);
      indices[y * w + x] = best;
      preview[si]     = pr[best];
      preview[si + 1] = pg[best];
      preview[si + 2] = pb[best];
      preview[si + 3] = 255;
    }
  }
  return { indices, preview };
}

function dither(imageData, palette, method) {
  if (KERNELS[method]) return ditherErrorDiffusion(imageData, palette, KERNELS[method]);
  if (BAYER[method])   return ditherOrdered(imageData, palette, BAYER[method]);
  return ditherNone(imageData, palette);
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
  const gamma = parseFloat($("gamma").value) || 1.0;
  const palette = PALETTES[state.panel.palette] || PALETTES.gray4;
  setStatus("Preparing preview...");
  await new Promise(r => setTimeout(r, 20));
  const t0 = performance.now();
  state.sourceRgba = rescaleFromCrop();
  applyGamma(state.sourceRgba, gamma);
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

function resetForNextUpload() {
  // Return to the "pick a photo" state so a second upload is one tap
  // away. Keep the success banner visible so the user has confirmation.
  if (state.bitmap) { state.bitmap.close(); state.bitmap = null; }
  state.crop = null;
  state.sourceRgba = null;
  state.currentIndices = null;
  state.currentPalette = null;
  state.file = null;
  state.fileBase = "photo";
  $("picker").value = "";
  $("pickerLabel").classList.remove("have");
  $("pickerText").textContent = "Tap to choose photo";
  $("cropCard").hidden = true;
  $("previewCard").hidden = true;
  $("upload").disabled = true;
  setStatus("");
}

async function onPick(e) {
  const file = e.target.files && e.target.files[0];
  if (!file) return;
  $("uploadedBanner").hidden = true;
  state.file = file;
  state.fileBase = stemFor(file.name);
  $("pickerLabel").classList.add("have");
  $("pickerText").textContent = file.name;
  $("previewCard").hidden = false;
  $("upload").disabled = true;
  setStatus("Decoding...");
  try {
    if (state.bitmap) { state.bitmap.close(); state.bitmap = null; }
    state.bitmap = await createImageBitmap(file,
      { imageOrientation: "from-image" });
  } catch (err) {
    setStatus("Could not decode this file: " + err.message, "err");
    return;
  }
  // Initial crop is a centred cover-fit rectangle at panel aspect.
  const { width: pw, height: ph } = state.panel;
  const r = coverRect(state.bitmap.width, state.bitmap.height, pw, ph);
  state.crop = {
    sx: Math.round(r.maxSx / 2),
    sy: Math.round(r.maxSy / 2),
    sw: r.sw,
    sh: r.sh,
  };
  $("cropCard").hidden = false;
  drawCropStage();
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
    $("uploadedBanner").hidden = false;
    resetForNextUpload();
    refreshPhotoList();
    window.scrollTo({ top: 0, behavior: "smooth" });
  } catch (err) {
    setStatus("Upload failed: " + err.message, "err");
    $("upload").disabled = false;
  }
}

$("picker").addEventListener("change", onPick);
$("dither").addEventListener("change", () => {
  if (state.bitmap) runPipeline();
});
let gammaTimer = null;
function updateGammaLabel() {
  $("gammaValue").textContent = parseFloat($("gamma").value).toFixed(2);
}
$("gamma").addEventListener("input", () => {
  updateGammaLabel();
  if (!state.bitmap) return;
  // Debounce so dragging the slider doesn't queue up a full-panel
  // dither on every step; 150 ms feels responsive without thrashing.
  clearTimeout(gammaTimer);
  gammaTimer = setTimeout(runPipeline, 150);
});
$("gammaReset").addEventListener("click", () => {
  $("gamma").value = "1.0";
  updateGammaLabel();
  if (state.bitmap) runPipeline();
});
$("upload").addEventListener("click", onUpload);
$("rebootBtn").addEventListener("click", async () => {
  const btn = $("rebootBtn");
  btn.disabled = true;
  btn.textContent = "Rebooting...";
  try {
    await fetch("/exit-portal", { method: "POST" });
  } catch (e) {
    // The AP goes down as the device reboots, so the fetch usually
    // rejects. That's fine - the request was sent.
  }
  btn.textContent = "Rebooting. You can close this tab.";
});
window.addEventListener("resize", () => { if (state.bitmap) drawCropStage(); });

// -- Your photos: list, thumbnail, delete --------------------------------

function escapeHtmlText(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", "\"": "&quot;", "'": "&#39;",
  }[c]));
}

function humanKB(bytes) {
  if (bytes < 1024) return bytes + " B";
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB";
  return (bytes / (1024 * 1024)).toFixed(1) + " MB";
}

async function refreshPhotoList() {
  const grid = $("photoGrid");
  const status = $("photosStatus");
  const photosDir = (state.panel && state.panel.photosDir) || "/photos";
  try {
    const r = await fetch("/photos-list.json", { cache: "no-store" });
    if (!r.ok) throw new Error("HTTP " + r.status);
    const items = await r.json();
    if (!Array.isArray(items) || items.length === 0) {
      grid.innerHTML = "";
      status.textContent = "No photos uploaded yet.";
      return;
    }
    items.sort((a, b) => a.name.localeCompare(b.name));
    status.textContent = items.length + " photo" +
      (items.length === 1 ? "" : "s") + " on the SD card.";
    // Cache-bust thumbnails with a fresh query string so a re-upload of
    // the same filename shows the new pixels instead of the cached one.
    // Points at /thumbnail (server-cached, small BMP) for speed - the
    // /download endpoint would ship the full multi-MB source per tile.
    const ts = Date.now();
    grid.innerHTML = items.map((it) => {
      const src = "/thumbnail?path=" +
        encodeURIComponent(photosDir + "/" + it.name) + "&t=" + ts;
      const safeName = escapeHtmlText(it.name);
      const dataName = escapeHtmlText(it.name);
      return (
        '<div class="photo-tile">' +
          '<img loading="lazy" alt="' + safeName + '" src="' + src + '">' +
          '<div class="meta">' +
            '<span class="name" title="' + safeName + '">' + safeName + '</span>' +
            '<span>' + humanKB(it.size) + '</span>' +
            '<button type="button" class="del" data-name="' + dataName + '">Delete</button>' +
          '</div>' +
        '</div>'
      );
    }).join("");
    grid.querySelectorAll("button.del").forEach((btn) => {
      btn.addEventListener("click", onDeletePhoto);
    });
  } catch (e) {
    status.textContent = "Cannot load photo list: " + e.message;
  }
}

async function onDeletePhoto(ev) {
  const btn = ev.currentTarget;
  const name = btn.getAttribute("data-name") || "";
  if (!name) return;
  if (!confirm("Delete \"" + name + "\"? This cannot be undone.")) return;
  btn.disabled = true;
  btn.textContent = "Deleting...";
  try {
    const fd = new FormData();
    fd.append("name", name);
    const r = await fetch("/delete-photo", { method: "POST", body: fd });
    if (!r.ok) throw new Error("HTTP " + r.status);
  } catch (e) {
    alert("Delete failed: " + e.message);
    btn.disabled = false;
    btn.textContent = "Delete";
    return;
  }
  refreshPhotoList();
}

bindCropDrag();
fetchPanel().then(refreshPhotoList);
</script>
</body>
</html>
)HTML";

}  // namespace sd_web_portal
