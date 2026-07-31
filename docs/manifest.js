// Resolves the latest GitHub release for danpodeanu/seeed-reterminal-E100X,
// finds the merged firmware asset that matches the selected board and
// application, and hands ESP Web Tools a data-URL manifest so no static
// per-release manifest files are needed.

const REPO = "danpodeanu/seeed-reterminal-E100X";
const LATEST_URL = `https://api.github.com/repos/${REPO}/releases/latest`;

const boardSel = document.getElementById("board");
const appSel = document.getElementById("app");
const installer = document.getElementById("installer");
const status = document.getElementById("status");

let releaseCache = null;

function setStatus(text, isError = false) {
  status.textContent = text;
  status.classList.toggle("error", isError);
}

function assetName(app, board) {
  return `firmware-${app}-${board}.bin`;
}

function findAsset(release, app, board) {
  const wanted = assetName(app, board);
  return (release.assets || []).find((a) => a.name === wanted) || null;
}

function buildManifest(release, asset, app, board) {
  return {
    name: `${app} for ${board}`,
    version: release.tag_name || "latest",
    home_assistant_domain: null,
    new_install_prompt_erase: false,
    builds: [
      {
        chipFamily: "ESP32-S3",
        parts: [
          {
            path: asset.browser_download_url,
            offset: 0,
          },
        ],
      },
    ],
  };
}

function encodeDataUrl(obj) {
  const json = JSON.stringify(obj);
  return `data:application/json;charset=utf-8,${encodeURIComponent(json)}`;
}

async function fetchLatestRelease() {
  const resp = await fetch(LATEST_URL, {
    headers: { Accept: "application/vnd.github+json" },
  });
  if (!resp.ok) {
    throw new Error(`GitHub API returned HTTP ${resp.status}`);
  }
  return resp.json();
}

function refresh() {
  if (!releaseCache) {
    installer.hidden = true;
    return;
  }
  const app = appSel.value;
  const board = boardSel.value;
  const asset = findAsset(releaseCache, app, board);
  if (!asset) {
    installer.hidden = true;
    setStatus(
      `No firmware for ${app} on ${board} in ${releaseCache.tag_name}.`,
      true
    );
    return;
  }
  const manifest = buildManifest(releaseCache, asset, app, board);
  installer.manifest = encodeDataUrl(manifest);
  installer.hidden = false;
  setStatus(
    `Ready to flash ${app} ${releaseCache.tag_name} for ${board}.`
  );
}

async function boot() {
  try {
    releaseCache = await fetchLatestRelease();
  } catch (err) {
    setStatus(`Couldn't load the latest release: ${err.message}`, true);
    return;
  }
  if (!releaseCache.assets || releaseCache.assets.length === 0) {
    setStatus(
      `Latest release ${releaseCache.tag_name} has no firmware assets yet.`,
      true
    );
    return;
  }
  refresh();
}

boardSel.addEventListener("change", refresh);
appSel.addEventListener("change", refresh);

boot();
