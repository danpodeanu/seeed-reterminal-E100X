// Resolves the latest GitHub release for danpodeanu/seeed-reterminal-E100X
// so the flasher can show the version tag, and constructs an ESP Web
// Tools manifest that points at the firmware binaries bundled with this
// Pages deployment under ./firmware/latest/.
//
// Fetching the binaries directly from the GitHub Release download URL
// does not work: GitHub redirects to a signed Azure Blob URL that
// omits Access-Control-Allow-Origin, so a cross-origin fetch from the
// flasher page fails with "Failed to fetch". The Pages workflow copies
// the release assets into /firmware/latest/ at deploy time; because the
// flasher and the binaries then share an origin, no CORS is needed.

const REPO = "danpodeanu/seeed-reterminal-E100X";
const LATEST_URL = `https://api.github.com/repos/${REPO}/releases/latest`;
const FIRMWARE_BASE = "./firmware/latest";

const boardSel = document.getElementById("board");
const appSel = document.getElementById("app");
const installer = document.getElementById("installer");
const installerErase = document.getElementById("installer-erase");
const status = document.getElementById("status");

let releaseTag = null;

function setStatus(text, isError = false) {
  status.textContent = text;
  status.classList.toggle("error", isError);
}

function assetName(app, board) {
  return `firmware-${app}-${board}-full.bin`;
}

function firmwareUrl(app, board) {
  return `${FIRMWARE_BASE}/${assetName(app, board)}`;
}

function buildManifest(url, version, app, board) {
  return {
    name: `${app} for ${board}`,
    version: version || "latest",
    home_assistant_domain: null,
    new_install_prompt_erase: false,
    builds: [
      {
        chipFamily: "ESP32-S3",
        parts: [{ path: url, offset: 0 }],
      },
    ],
  };
}

function encodeDataUrl(obj) {
  const json = JSON.stringify(obj);
  return `data:application/json;charset=utf-8,${encodeURIComponent(json)}`;
}

async function fetchLatestTag() {
  try {
    const resp = await fetch(LATEST_URL, {
      headers: { Accept: "application/vnd.github+json" },
    });
    if (!resp.ok) return null;
    const release = await resp.json();
    return release.tag_name || null;
  } catch (err) {
    return null;
  }
}

async function firmwarePresent(url) {
  try {
    const resp = await fetch(url, { method: "HEAD" });
    return resp.ok;
  } catch (err) {
    return false;
  }
}

async function refresh() {
  const app = appSel.value;
  const board = boardSel.value;
  const url = firmwareUrl(app, board);
  installer.hidden = true;
  installerErase.hidden = true;
  setStatus("Checking firmware…");
  const ok = await firmwarePresent(url);
  if (!ok) {
    setStatus(
      `No firmware for ${app} on ${board} at ${releaseTag || "the latest release"}.`,
      true
    );
    return;
  }
  const manifest = buildManifest(url, releaseTag, app, board);
  const manifestUrl = encodeDataUrl(manifest);
  installer.manifest = manifestUrl;
  installer.hidden = false;
  installerErase.manifest = manifestUrl;
  installerErase.hidden = false;
  setStatus(
    `Ready to flash ${app} ${releaseTag || "latest"} for ${board}.`
  );
}

async function boot() {
  releaseTag = await fetchLatestTag();
  await refresh();
}

boardSel.addEventListener("change", refresh);
appSel.addEventListener("change", refresh);

boot();

