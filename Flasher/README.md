# Decentralized Dual Miners — Local USB Flasher

A **locally-run** web dashboard to flash BitAxe / NerdAxe / NerdQAxe
over the USB (USB-JTAG/serial) port — the same technique the online flashers
([bitaxe-web-flasher](https://bitaxeorg.github.io/bitaxe-web-flasher/),
[flasher.bitronics.store](https://flasher.bitronics.store/)) use: the browser's
**Web Serial API** + **[esptool-js](https://github.com/espressif/esptool-js)**.
Everything runs on your machine; no firmware or data is uploaded anywhere.

## Run it

```bash
cd Flasher
python serve.py        # opens http://localhost:8000/index.html
```
Windows: double-click **`serve.bat`**.

Then in **Chrome** or **Edge** (desktop):
1. Pick your **device preset**.
2. Choose the firmware **`.bin`** and confirm the **offset** (factory images = `0x0`).
3. Click **Connect & Flash** and select the serial port in the browser prompt.
4. Use **Serial Monitor** to watch the boot log and verify pool connection.

## Requirements & rules

- **Chrome or Edge, desktop.** Web Serial is not in Firefox/Safari.
- **Must be served from `http://localhost`** (that's what `serve.py` does). Web Serial
  is blocked on `file://`. `localhost`/`127.0.0.1` are treated as secure contexts, so
  plain HTTP there is fine.
- **Internet on first load:** `esptool-js` is imported from a pinned CDN
  (`esptool-js@0.5.4`) and then cached by the browser. For a fully offline flasher,
  vendor it (see below).
- Use the device's **USB** port. If it doesn't appear in the port picker, install the
  USB-serial driver for your board (native ESP32-S3 USB usually needs none).

## Where do the `.bin` files come from?

You build them (this environment can't):

| Device | Build | Image to select |
|--------|-------|-----------------|
| BitAxe | `cd ../BitAxe-ESP-Miner && idf.py build` | `build/esp-miner-factory-*.bin` @ `0x0` |
| NerdAxe | `cd ../NerdAxe-NerdQAxe-ESP-Miner && BOARD=NERDAXE idf.py build` | factory/merged bin @ `0x0` |
| NerdQAxe | `… BOARD=NERDQAXEPLUS idf.py build` | factory/merged bin @ `0x0` |

Full step-by-step (with post-flash dual-mining verification) is in
[../FLASHING_AND_VERIFICATION.md](../FLASHING_AND_VERIFICATION.md).

## Troubleshooting

- **"Web Serial not supported"** → you're in Firefox/Safari or on `file://`. Use
  Chrome/Edge via `python serve.py`.
- **Connect hangs / fails** → put the board in download mode: hold **BOOT**, tap
  **RESET**, release **BOOT**, then retry. Try **115200** baud.
- **Flashed but nothing happens** → power-cycle the device; open Serial Monitor to see
  the boot log.

## Fully offline (optional): vendor esptool-js

1. Download the ESM bundle:
   `https://cdn.jsdelivr.net/npm/esptool-js@0.5.4/+esm` → save as `esptool-js.js` here.
2. In `flasher.js`, change the import to:
   `import { ESPLoader, Transport } from "./esptool-js.js";`

Now the flasher needs no internet.

## Safety

This tool only writes firmware **you** select to a device **you** connect. It never
reads or transmits your data off-device. Erasing flash (the "Erase Flash Only" button
and the "Erase entire flash first" checkbox) wipes saved Wi-Fi and pool settings —
you'll reconfigure them on next boot.
