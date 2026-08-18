# Flashing & Verification Checklist

Per-device steps to build, flash, and verify dual-pool mining + the pool-password
field for each firmware in this deliverable. Two flashing routes are given:
**A) CLI** (`idf.py` / `esptool` / `pio`) and **B) the local web flasher** in
[`Local Flasher/`](Local%20Flasher/) (USB, Chrome/Edge, no toolchain needed once you have a `.bin`).

> Connection: use the device's **USB (USB-JTAG/serial)** port. On first connect,
> install the USB-serial driver if the port doesn't appear (ESP32-S3 native USB
> usually needs none; boards with a CP2102/CH340 bridge need that driver).

---

## Device 1 — BitAxe (ESP-Miner, new dual-pool firmware)

### Build
- [ ] Install ESP-IDF (v5.x) and Node/npm (for the web UI).
- [ ] `cd BitAxe-ESP-Miner`
- [ ] (Optional, recommended) run the logic tests:
      `cd components/dual_pool/test_host && make run` → expect `ALL dual_pool host tests passed`.
- [ ] `idf.py set-target esp32s3`
- [ ] `idf.py build`
- [ ] Confirm the merged image exists (e.g. `build/esp-miner-factory-*.bin` or
      `build/esp-miner.bin` + partitions). The **factory** bin flashes at `0x0`.

### Flash — A) CLI
- [ ] `idf.py -p <PORT> flash monitor`  (or `esptool.py -p <PORT> write_flash 0x0 build/esp-miner-factory-*.bin`)

### Flash — B) Web flasher
- [ ] Start it: `cd "Local Flasher" && python serve.py` → opens `http://localhost:8000`.
- [ ] Pick device preset **BitAxe (ESP32-S3, factory @0x0)**.
- [ ] Select your `esp-miner-factory-*.bin`, click **Connect & Flash**, pick the port.

### First-boot config
- [ ] Join the `Bitaxe_XXXX` AP (or the device's portal) and set Wi-Fi.
- [ ] Open the device web UI → **Pool** settings.
- [ ] Confirm **Pool Password** field is present for Pool A (requirement #2).
- [ ] Scroll to **Dual Mining (Simultaneous Pool B)**:
  - [ ] set **Enable Dual Mining** = on
  - [ ] **Split Interval (ms)** = 500
  - [ ] **Pool A Share %** = 70 (or your ratio)
  - [ ] Fill **Pool B** host/port/user/**Pool Password** (+ optional Pool B Failover)
  - [ ] Save, then **Restart** (banner reminds you).

### Verify dual mining
- [ ] `idf.py -p <PORT> monitor` (or the web flasher's **Serial Monitor**).
- [ ] See both tasks connect: `stratum_v1_task` (Pool A) **and** `stratum poolb` (Pool B),
      and stay connected (no reconnect churn).
- [ ] `curl http://<device-ip>/api/system/info` → check `dualEnable:true`,
      `poolBUrl`, `poolBSharesAccepted` climbing.
- [ ] Over ~15 min, confirm accepted shares on **both** pools' dashboards, ~in your ratio
      (`poolASharesAccepted` vs `poolBSharesAccepted`).
- [ ] **Failover:** block Pool B's primary host → Pool B switches to its failover endpoint,
      Pool A keeps mining uninterrupted. Restore → Pool B returns to primary.
- [ ] **Dual-off sanity:** set Enable Dual Mining = off, save/restart → behaves exactly
      like stock (Pool A only, `stratum poolb` idles).

---

## Device 2 — NerdAxe & NerdQAxe (ESP-Miner-NerdQAxePlus fork)

Dual-pool + per-pool passwords are **native** here (no code changes). See
[NerdAxe-NerdQAxe-ESP-Miner/DUAL_MINING_NOTES.md](NerdAxe-NerdQAxe-ESP-Miner/DUAL_MINING_NOTES.md).

### Build (board selected by env var)
- [ ] `cd NerdAxe-NerdQAxe-ESP-Miner`
- [ ] NerdAxe: `export BOARD=NERDAXE`  •  NerdQAxe: `export BOARD=NERDQAXEPLUS`
      (or `NERDQAXEPLUS2` for the ++). Windows PowerShell: `$env:BOARD="NERDAXE"`.
- [ ] `idf.py build`
- [ ] Note the factory/merged bin path (flash at `0x0`).

### Flash — A) CLI
- [ ] `idf.py -p <PORT> flash monitor`

### Flash — B) Web flasher
- [ ] `cd "Local Flasher" && python serve.py`
- [ ] Preset **NerdAxe / NerdQAxe (ESP32-S3, factory @0x0)** → select bin → **Connect & Flash**.

### Config & verify
- [ ] Web UI → **Settings**: set **Pool Mode = Dual**.
- [ ] Adjust the **Pool Balance** slider (e.g. 70 → 70%/30%).
- [ ] Enter **Password** for both the primary and secondary pool.
- [ ] Save + restart.
- [ ] Home page shows a **dual-pool split**; both pools connect and accept shares in the
      configured balance. (`GET /api/system` → `poolMode:1`, per-pool `accepted`.)

---

## Device 3 — NerdMiner_v2 (CPU lottery miner)

Pool Password is **native**; dual-pool is out of scope (see
[NerdMiner_v2/DUAL_MINING_NOTES.md](NerdMiner_v2/DUAL_MINING_NOTES.md)).

### Build & flash (PlatformIO)
- [ ] `cd NerdMiner_v2`
- [ ] Pick your board's env in `platformio.ini`.
- [ ] `pio run -e <env>` then `pio run -e <env> -t upload` (CLI route).
- [ ] Web-flasher route: build produces a bin under `.pio/build/<env>/` — in the
      **Flasher**, use preset **NerdMiner_v2 (Custom / verify offset)** and select that bin.
      (NerdMiner_v2 images are typically flashed at `0x0` when merged; if you only have
      `firmware.bin` use `0x10000` + bootloader `0x0` + partitions `0x8000`.)

### Config & verify
- [ ] Join the NerdMiner Wi-Fi config portal.
- [ ] Set Wi-Fi + **Pool** (address/port/wallet) and the **Pool password** field.
- [ ] Save; device reboots and connects; serial log shows `mining.authorize` with your
      password and incoming `mining.notify` jobs.

---

## Quick reference — flash offsets

| Firmware | Image | Offset |
|----------|-------|--------|
| BitAxe / NerdAxe / NerdQAxe | `*-factory-*.bin` (merged) | `0x0` |
| ESP-IDF split images | bootloader / partitions / app | `0x0` / `0x8000` / `0x10000` |
| NerdMiner_v2 merged | firmware merged bin | `0x0` |

If a flash fails to start, hold **BOOT**, tap **RESET**, release **BOOT** to enter
download mode, then retry.
