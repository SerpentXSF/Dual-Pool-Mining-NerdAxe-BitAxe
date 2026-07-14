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

## NerdAxe / NerdQAxe / NerdMiner_v2 — use official prebuilt bins

These are unmodified (dual-pool + password already native), so download the official
release images instead of building:
- NerdAxe / NerdQAxe: https://github.com/shufps/ESP-Miner-NerdQAxePlus/releases
  (pick the factory bin for `nerdaxe` / `nerdqaxeplus` / `nerdqaxeplus2`).
- NerdMiner_v2: https://github.com/BitMaker-hub/NerdMiner_v2/releases (pick your board).
