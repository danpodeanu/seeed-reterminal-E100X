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
// E1001-E1004 share one boot chain. E1005 has a model-specific 32 MB
// bootloader and partition table, published with the board suffix.
const E1005_BOARD = "reterminal_e1005";

const boardSel = document.getElementById("board");
const appSel = document.getElementById("app");
const gamesOption = appSel.querySelector('option[value="games"]');
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

function appSupportedOnBoard(app, board) {
  return app !== "games" || board === E1005_BOARD;
}

function bootAssetUrl(stem, board) {
  const suffix = board === E1005_BOARD ? `-${board}` : "";
  return `${FIRMWARE_BASE}/${stem}${suffix}.bin`;
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
  const bootloaderUrl = bootAssetUrl("bootloader", board);
  const partitionsUrl = bootAssetUrl("partitions", board);
  const bootApp0Url = bootAssetUrl("boot_app0", board);
  return {
    name: `${app} for ${board}`,
    version: version || "latest",
    home_assistant_domain: null,
    new_install_prompt_erase: true,
    builds: [
      {
        chipFamily: "ESP32-S3",
        parts: [
          { path: bootloaderUrl, offset: 0x0000 },
          { path: partitionsUrl, offset: 0x8000 },
          { path: bootApp0Url, offset: 0xe000 },
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
  installer.hidden = true;
  if (!appSupportedOnBoard(app, board)) {
    setStatus("Games is available only for reTerminal E1005.", true);
    return;
  }
  const otaUrl = otaFirmwareUrl(app, board);
  const requiredUrls = [
    otaUrl,
    bootAssetUrl("bootloader", board),
    bootAssetUrl("partitions", board),
    bootAssetUrl("boot_app0", board),
  ];
  setStatus("Checking firmware…");
  const available = await Promise.all(requiredUrls.map(firmwarePresent));
  if (available.some((present) => !present)) {
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

function updateAppAvailability() {
  gamesOption.disabled = !appSupportedOnBoard("games", boardSel.value);
  if (gamesOption.disabled && appSel.value === "games") {
    appSel.value = "weather-viewer";
  }
}

async function boot() {
  releaseTag = await fetchLatestTag();
  updateAppAvailability();
  await refresh();
}

boardSel.addEventListener("change", async () => {
  updateAppAvailability();
  await refresh();
});
appSel.addEventListener("change", refresh);

boot();
