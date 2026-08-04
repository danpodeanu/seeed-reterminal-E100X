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
// Reset otadata to point at app0 whenever we (re)flash the app slot in
// preserve-settings mode. Same blob for every board.
const BOOT_APP0_URL = `${FIRMWARE_BASE}/boot_app0.bin`;

const boardSel = document.getElementById("board");
const appSel = document.getElementById("app");
const installerPreserve = document.getElementById("installer-preserve");
const installer = document.getElementById("installer");
const installerErase = document.getElementById("installer-erase");
const status = document.getElementById("status");

let releaseTag = null;

function setStatus(text, isError = false) {
  status.textContent = text;
  status.classList.toggle("error", isError);
}

function fullAssetName(app, board) {
  return `firmware-${app}-${board}-full.bin`;
}

function otaAssetName(app, board) {
  return `firmware-${app}-${board}-ota.bin`;
}

function fullFirmwareUrl(app, board) {
  return `${FIRMWARE_BASE}/${fullAssetName(app, board)}`;
}

function otaFirmwareUrl(app, board) {
  return `${FIRMWARE_BASE}/${otaAssetName(app, board)}`;
}

// Manifest for "delete settings" / "erase" - single merged image at 0x0
// that spans NVS at 0x9000 and therefore wipes it.
function buildFullManifest(url, version, app, board) {
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

// Manifest for "preserve settings" - writes only otadata (0xE000) and
// the app-only image (0x10000), skipping bootloader (0x0) / partitions
// (0x8000) / NVS (0x9000). NVS survives, Wi-Fi credentials keep working.
//
// new_install_prompt_erase MUST be true here: ESP Web Tools' default for
// firmware without Improv Serial is to call esploader.eraseFlash() before
// writing any parts, which would wipe the bootloader / partition table
// and boot-loop the device with "invalid header: 0xffffffff". With this
// flag set, the flasher shows an ASK_ERASE prompt with the checkbox off
// by default -- clicking Next through it skips the pre-write erase.
function buildPreserveManifest(otaUrl, version, app, board) {
  return {
    name: `${app} for ${board} (preserve settings)`,
    version: version || "latest",
    home_assistant_domain: null,
    new_install_prompt_erase: true,
    builds: [
      {
        chipFamily: "ESP32-S3",
        parts: [
          { path: BOOT_APP0_URL, offset: 0xe000 },
          { path: otaUrl, offset: 0x10000 },
        ],
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
  const fullUrl = fullFirmwareUrl(app, board);
  const otaUrl = otaFirmwareUrl(app, board);
  installerPreserve.hidden = true;
  installer.hidden = true;
  installerErase.hidden = true;
  setStatus("Checking firmware…");
  const [fullOk, otaOk] = await Promise.all([
    firmwarePresent(fullUrl),
    firmwarePresent(otaUrl),
  ]);
  if (!fullOk) {
    setStatus(
      `No firmware for ${app} on ${board} at ${releaseTag || "the latest release"}.`,
      true
    );
    return;
  }
  const fullManifest = encodeDataUrl(
    buildFullManifest(fullUrl, releaseTag, app, board)
  );
  installer.manifest = fullManifest;
  installer.hidden = false;
  installerErase.manifest = fullManifest;
  installerErase.hidden = false;
  if (otaOk) {
    installerPreserve.manifest = encodeDataUrl(
      buildPreserveManifest(otaUrl, releaseTag, app, board)
    );
    installerPreserve.hidden = false;
  }
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

