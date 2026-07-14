// Local USB flasher for the Decentralized Dual Miners firmwares.
// Uses the Web Serial API + esptool-js (same stack as the online BitAxe flashers).
// Runs entirely in the browser; nothing is uploaded anywhere.

import { ESPLoader, Transport } from "https://cdn.jsdelivr.net/npm/esptool-js@0.5.4/+esm";

// ---- device presets ---------------------------------------------------------
const PRESETS = [
  { id: "bitaxe",    name: "BitAxe (ESP32-S3) — factory .bin @ 0x0",
    offset: "0x0", note: "Select esp-miner-factory-*.bin from BitAxe-ESP-Miner/build/. Flashes at 0x0 (keeps NVS config)." },
  { id: "nerdaxe",   name: "NerdAxe (ESP32-S3) — factory .bin @ 0x0",
    offset: "0x0", note: "Build with BOARD=NERDAXE. Select the merged/factory bin. Flashes at 0x0." },
  { id: "nerdqaxe",  name: "NerdQAxe / NerdQAxe+ (ESP32-S3) — factory .bin @ 0x0",
    offset: "0x0", note: "Build with BOARD=NERDQAXEPLUS (or NERDQAXEPLUS2). Select the merged/factory bin. Flashes at 0x0." },
  { id: "nerdminer", name: "NerdMiner_v2 (ESP32/S3) — merged .bin @ 0x0",
    offset: "0x0", note: "From .pio/build/<env>/. If you only have firmware.bin (not merged), use offset 0x10000 and flash bootloader/partitions separately." },
  { id: "custom",    name: "Custom — choose file and offset",
    offset: "0x0", note: "Pick any .bin and set the flash offset manually." },
];

// ---- element refs -----------------------------------------------------------
const $ = (id) => document.getElementById(id);
const presetSel = $("preset"), presetNote = $("presetNote"), offsetEl = $("offset");
const fileEl = $("file"), baudEl = $("baud"), eraseAllEl = $("eraseAll");
const flashBtn = $("flashBtn"), eraseBtn = $("eraseBtn");
const barEl = $("bar"), statusEl = $("status"), logEl = $("log");
const monBtn = $("monBtn"), monStop = $("monStop"), logClear = $("logClear");

// ---- console logging --------------------------------------------------------
function log(s) { logEl.textContent += s; logEl.scrollTop = logEl.scrollHeight; }
function logln(s = "") { log(s + "\n"); }
function setStatus(s) { statusEl.textContent = s; }
function setProgress(pct) { barEl.style.width = Math.max(0, Math.min(100, pct)) + "%"; }

// esptool-js terminal interface
const terminal = {
  clean() { /* keep history */ },
  writeLine(data) { logln(data); },
  write(data) { log(data); },
};

// ---- init UI ----------------------------------------------------------------
for (const p of PRESETS) {
  const o = document.createElement("option");
  o.value = p.id; o.textContent = p.name; presetSel.appendChild(o);
}
function applyPreset() {
  const p = PRESETS.find(x => x.id === presetSel.value) || PRESETS[0];
  offsetEl.value = p.offset;
  presetNote.textContent = p.note;
}
presetSel.addEventListener("change", applyPreset);
applyPreset();

// Web Serial support check
if (!("serial" in navigator)) {
  const b = $("compat");
  b.hidden = false;
  b.textContent = "This browser does not support the Web Serial API. Use desktop Chrome or Edge, served from http://localhost (run: python serve.py).";
  flashBtn.disabled = eraseBtn.disabled = monBtn.disabled = true;
}

// ---- helpers ----------------------------------------------------------------
function parseOffset(s) {
  s = (s || "").trim();
  const n = s.toLowerCase().startsWith("0x") ? parseInt(s, 16) : parseInt(s, 10);
  if (Number.isNaN(n) || n < 0) throw new Error("Invalid flash offset: " + s);
  return n;
}
function busy(on) {
  flashBtn.disabled = eraseBtn.disabled = on;
  presetSel.disabled = fileEl.disabled = offsetEl.disabled = baudEl.disabled = on;
}

async function withLoader(fn) {
  let transport, esploader, port;
  try {
    port = await navigator.serial.requestPort();
    transport = new Transport(port, true);
    const baud = parseInt(baudEl.value, 10);
    esploader = new ESPLoader({ transport, baudrate: baud, terminal });
    logln("Connecting to device...");
    const chip = await esploader.main();
    logln("Detected chip: " + chip);
    await fn(esploader);
    try { await esploader.hardReset(); } catch (_) { /* older API: ignore */ }
    logln("Done. You can power-cycle the device if it does not reboot automatically.");
  } finally {
    try { if (transport) await transport.disconnect(); } catch (_) {}
  }
}

// ---- flash ------------------------------------------------------------------
async function doFlash() {
  const file = fileEl.files[0];
  if (!file) { setStatus("Choose a .bin file first."); return; }
  let address;
  try { address = parseOffset(offsetEl.value); }
  catch (e) { setStatus(e.message); return; }

  busy(true); setProgress(0); setStatus("Reading firmware...");
  try {
    const data = new Uint8Array(await file.arrayBuffer());
    logln(`\nFlashing ${file.name} (${data.length.toLocaleString()} bytes) @ 0x${address.toString(16)}`);
    await withLoader(async (esploader) => {
      await esploader.writeFlash({
        fileArray: [{ data, address }],
        flashSize: "keep",
        flashMode: "keep",
        flashFreq: "keep",
        eraseAll: eraseAllEl.checked,
        compress: true,
        reportProgress: (fileIndex, written, total) => {
          const pct = Math.round((written / total) * 100);
          setProgress(pct);
          setStatus(`Writing... ${pct}%`);
        },
      });
    });
    setProgress(100); setStatus("✅ Flash complete.");
  } catch (e) {
    console.error(e);
    logln("\nERROR: " + (e && e.message ? e.message : e));
    setStatus("❌ Flash failed — see console. Try 115200 baud or enter download mode (hold BOOT, tap RESET).");
  } finally {
    busy(false);
  }
}

// ---- erase only -------------------------------------------------------------
async function doErase() {
  if (!confirm("Erase the ENTIRE flash? This wipes saved Wi-Fi and pool settings.")) return;
  busy(true); setProgress(0); setStatus("Erasing...");
  try {
    await withLoader(async (esploader) => {
      logln("\nErasing entire flash...");
      await esploader.eraseFlash();
    });
    setProgress(100); setStatus("✅ Flash erased.");
  } catch (e) {
    console.error(e);
    logln("\nERROR: " + (e && e.message ? e.message : e));
    setStatus("❌ Erase failed — see console.");
  } finally {
    busy(false);
  }
}

// ---- serial monitor ---------------------------------------------------------
let monPort = null, monReader = null, monKeep = false;
async function openMonitor() {
  try {
    monPort = await navigator.serial.requestPort();
    await monPort.open({ baudRate: 115200 });
    monKeep = true;
    monBtn.disabled = true; monStop.disabled = false;
    setStatus("Serial monitor open @115200.");
    logln("\n--- serial monitor (115200) ---");
    const dec = new TextDecoder();
    monReader = monPort.readable.getReader();
    while (monKeep) {
      const { value, done } = await monReader.read();
      if (done) break;
      if (value) log(dec.decode(value));
    }
  } catch (e) {
    logln("\nMonitor error: " + (e && e.message ? e.message : e));
  } finally {
    await closeMonitor();
  }
}
async function closeMonitor() {
  monKeep = false;
  try { if (monReader) { await monReader.cancel(); monReader.releaseLock(); } } catch (_) {}
  try { if (monPort) await monPort.close(); } catch (_) {}
  monReader = null; monPort = null;
  monBtn.disabled = false; monStop.disabled = true;
}

// ---- wire up ----------------------------------------------------------------
flashBtn.addEventListener("click", doFlash);
eraseBtn.addEventListener("click", doErase);
monBtn.addEventListener("click", openMonitor);
monStop.addEventListener("click", closeMonitor);
logClear.addEventListener("click", () => { logEl.textContent = ""; });

logln("Ready. Pick a device preset, choose your .bin, and click Connect & Flash.");
