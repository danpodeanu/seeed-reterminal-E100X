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
// Shared boot chain -- byte-identical across every env, published once
// per release. Written alongside the per-env -ota.bin at the four
// arduino-esp32 partition offsets so a "keep settings" flash lays down
// bootloader / partitions / otadata / app while leaving NVS (0x9000)
// and SPIFFS (0x610000) untouched.
const BOOTLOADER_URL = `${FIRMWARE_BASE}/bootloader.bin`;
const PARTITIONS_URL = `${FIRMWARE_BASE}/partitions.bin`;
const BOOT_APP0_URL = `${FIRMWARE_BASE}/boot_app0.bin`;

const boardSel = document.getElementById("board");
const appSel = document.getElementById("app");
const installer = document.getElementById("installer");
const status = document.getElementById("status");

let releaseTag = null;

function setStatus(text, isError = false) {
  status.textContent = text;
  status.classList.toggle("error", isError);
}

function otaAssetName(app, board) {
  return `firmware-${app}-${board}-ota.bin`;
}

function otaFirmwareUrl(app, board) {
  return `${FIRMWARE_BASE}/${otaAssetName(app, board)}`;
}

// Single-button manifest. Writes bootloader / partitions / otadata / app
// as four separate parts so the two ASK_ERASE outcomes have clean
// semantics:
//
// * checkbox unchecked -> esptool-js sector-erases only the sectors it
//   writes. NVS (0x9000..0xDFFF) and SPIFFS (0x610000+) are never
//   touched, so Wi-Fi credentials and cached data survive.
// * checkbox checked -> ESP Web Tools calls esploader.eraseFlash()
//   first (full chip erase), then writes the same four parts. NVS and
//   SPIFFS are wiped; the device comes up in factory-fresh state.
//
// new_install_prompt_erase MUST be true here: with it false, ESP Web
// Tools' _renderDashboardNoImprov path (our firmware doesn't do Improv
// Serial) auto-calls _startInstall(true) -> esploader.eraseFlash(),
// removing the user's ability to keep NVS at all.
function buildManifest(otaUrl, version, app, board) {
  return {
    name: `${app} for ${board}`,
    version: version || "latest",
    home_assistant_domain: null,
    new_install_prompt_erase: true,
    builds: [
      {
        chipFamily: "ESP32-S3",
        parts: [
          { path: BOOTLOADER_URL, offset: 0x0000 },
          { path: PARTITIONS_URL, offset: 0x8000 },
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
  const otaUrl = otaFirmwareUrl(app, board);
  installer.hidden = true;
  setStatus("Checking firmware…");
  const otaOk = await firmwarePresent(otaUrl);
  if (!otaOk) {
    setStatus(
      `No firmware for ${app} on ${board} at ${releaseTag || "the latest release"}.`,
      true
    );
    return;
  }
  installer.manifest = encodeDataUrl(
    buildManifest(otaUrl, releaseTag, app, board)
  );
  installer.hidden = false;
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
