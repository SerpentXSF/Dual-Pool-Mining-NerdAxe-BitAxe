# Firmware Binaries

## BitAxe (custom dual-pool firmware) — prebuilt here ✅

`BitAxe/esp-miner-factory.bin` is the **ready-to-flash** image for BitAxe (ESP32-S3),
built from this deliverable's `BitAxe-ESP-Miner/` source. Flash it at offset **`0x0`**
(it's the full 16 MB image: bootloader + partition table + app + web UI + OTA data).

| File | Use | Flash offset | Size |
|------|-----|--------------|------|
| `BitAxe/esp-miner-factory.bin` | Fresh flash / recovery (full image) | `0x0` | 15.8 MB |
| `BitAxe/esp-miner-ota.bin` | App-only (for the device's web **OTA update**) | n/a (upload via UI) | 1.7 MB |

**SHA-256**
```
esp-miner-factory.bin  9f3c41ecb90807eb7c357cee4d1b12c4e280091d40328d9915d97c9bd304919f
esp-miner-ota.bin      890dac9ffe8e718ec4da2037206934a8c3c28651ab0d45c5a6a7b0a752d7240a
```

**Build provenance**
- Source: `BitAxe-ESP-Miner/` (commit at build time).
- Toolchain: `espressif/idf:v5.5.3` (Docker), web UI built with Node 24 / Angular
  production build.
- Submodule: `components/libsecp256k1/libsecp256k1` = bitcoin-core/secp256k1 @
  `0cdc758a56360bf58a851fe91085a327ec97685a` (upstream-pinned commit).
- App size 0x1a0310 (~1.7 MB), 59% of the app partition free.
- This build **verifies the full firmware compiles** (all dual-pool C code + the
  Angular Dual Mining UI). It has not been run on physical hardware here.

### Flash it
- **Web flasher** (easiest): `cd ../Flasher && python serve.py` → preset
  **BitAxe (ESP32-S3) factory @0x0** → select `esp-miner-factory.bin` → Connect & Flash.
- **CLI:** `esptool.py --chip esp32s3 -p <PORT> write_flash 0x0 esp-miner-factory.bin`

Then configure dual mining in the web UI (see `../FLASHING_AND_VERIFICATION.md`).

## NerdAxe & NerdQAxe — prebuilt here too ✅

Built from `NerdAxe-NerdQAxe-ESP-Miner/` (unmodified fork; dual-pool + password are
native). Flash the factory bin at offset **`0x0`**.

| File | Device | How to apply |
|------|--------|--------------|
| `NerdAxe/esp-miner-factory-nerdaxe.bin` | NerdAxe | USB flash @ `0x0` (one-time bootstrap) |
| `NerdAxe/esp-miner-nerdaxe.bin` | NerdAxe | **OTA firmware** — Settings → Manual Update → Firmware |
| `NerdAxe/www.bin` | NerdAxe | **OTA web UI** — Settings → Manual Update → Web UI |
| `NerdQAxe/esp-miner-factory-nerdqaxeplus.bin` | NerdQAxe+ | USB flash @ `0x0` |
| `NerdQAxe/esp-miner-ota-nerdqaxeplus.bin` | NerdQAxe+ | OTA app bin |

### OTA workflow (NerdAxe)
This build adds a **Manual Update (upload .bin)** section under **Settings → Release &
Update**. USB-flash `esp-miner-factory-nerdaxe.bin` **once** to get it; after that,
update over the air with **no USB**:
- Dashboard/UI change → upload **`www.bin`** (Web UI).
- Firmware change → upload **`esp-miner-nerdaxe.bin`** (Firmware; the device reboots).
The upload filenames must match exactly (`www.bin`, `esp-miner-nerdaxe.bin`). OTP is
optional — uploads work without it.

**SHA-256 (factory)**
```
esp-miner-factory-nerdaxe.bin        a53bb91411e7bddf98f54919816fc304101d0227ee8d69593be2935175f0f5a0
esp-miner-factory-nerdqaxeplus.bin   e80fd8260d2985e17fbdd8e71ac39fa6332c824ed4e9700ccc2bba731ea64cdd
```

**Build provenance:** `shufps/esp-idf-builder:0.0.1` (ESP-IDF v5.3.3 + Node 20),
`BOARD=NERDAXE` / `BOARD=NERDQAXEPLUS`, secp256k1 submodule @ `0cdc758`. Each build
compiled the firmware **and** its Angular web UI. After flashing, enable dual mining
in the UI (**Pool Mode = Dual** + **Pool Balance** slider) — see
`../NerdAxe-NerdQAxe-ESP-Miner/DUAL_MINING_NOTES.md`.

> Need another board variant (NERDQAXEPLUS2, NERDOCTAXE, gamma, etc.)? The official
> releases cover them: https://github.com/shufps/ESP-Miner-NerdQAxePlus/releases
> — or ask and I'll build that `BOARD=` from source.

## NerdMiner_v2 — official prebuilt bins

Unmodified (password native, dual-pool out of scope). Use the official release for
your board: https://github.com/BitMaker-hub/NerdMiner_v2/releases
