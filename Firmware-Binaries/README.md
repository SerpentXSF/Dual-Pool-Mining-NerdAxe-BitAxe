# Firmware Binaries

## BitAxe (custom dual-pool firmware) — prebuilt here ✅

`BitAxe/esp-miner-factory.bin` is the **ready-to-flash** image for BitAxe (ESP32-S3),
built from this deliverable's `BitAxe-ESP-Miner/` source. Flash it at offset **`0x0`**
(it's the full 16 MB image: bootloader + partition table + app + web UI + OTA data).

| File | Use | Flash offset | Size |
|------|-----|--------------|------|
| `BitAxe/esp-miner-factory.bin` | Fresh flash / recovery (full image) | `0x0` | 15.8 MB |
| `BitAxe/esp-miner.bin` | App-only — device web **OTA** (Firmware) | n/a (upload via UI) | 1.7 MB |
| `BitAxe/www.bin` | Web UI — device web **OTA** (Web UI) | n/a (upload via UI) | 3.0 MB |

**SHA-256**
```
esp-miner-factory.bin  e55a100b0bc6ef58f4bfaf0f890a87e9f18eb9e82574101d6ba01c623dae8f44
esp-miner.bin          6cc641f48d6837fe194db3d9610969732f0b4698884618d6669beacfb48a53de
www.bin                8e87dd8768cd9804c60b5b7af7514fc3cbb3b92ded0eebabe490816b24d8fbf9
```

**Build provenance**
- Source: `BitAxe-ESP-Miner/` (commit at build time).
- Toolchain: `espressif/idf:v5.5.3` (Docker), web UI built with Node 24 / Angular
  production build.
- Submodule: `components/libsecp256k1/libsecp256k1` = bitcoin-core/secp256k1 @
  `0cdc758a56360bf58a851fe91085a327ec97685a` (upstream-pinned commit).
- **SerpentX branding (web UI):** the AxeOS header carries the SerpentX wordmark, and a
  top-right **accent-colour picker** (palette icon; **AxeOS Blue default**, SerpentX Gold +
  others opt-in, saved in the browser) re-themes the dashboard. Also makes the dashboard
  **widget resize grip** clearly visible in edit mode (accent corner-grip). UI-only —
  `esp-miner.bin` firmware is unchanged, so an **OTA "Web UI" upload of `www.bin` is all
  that's needed** for the new look.
- App size 0x1a07d0 (~1.7 MB), 59% of the app partition free.
- Adds the Fable-review efficiency fixes: **pool-aware `clean_jobs`** (a Pool A clean no
  longer invalidates Pool B's in-flight ASIC jobs — the main recurring dual-mode share
  loss), **A→B slice donation** (mine Pool B instead of idling/wasting when Pool A has no
  work or its socket is down), **Pool B drain-to-newest** (never mine a stale B template),
  and a **`poolBStaleDrops`** diagnostic counter in `/api/system`.
- Includes the maintenance/error-rate hardening: **live-tunable Split Interval / ratio /
  dual-enable** (no reboot), **Pool B version-mask coherence**, **dropped-share recovery**
  (a sub-difficulty nonce whose job slot was reused across pools is re-tested against the
  other live templates and submitted to the pool that owns it), and a **default Split
  Interval of 3000 ms** (field-tested: 500 ms → 3000 ms took one miner's error rate from
  ~20% to ~3%). Earlier
  dual-pool builds were verified mining to both pools on real hardware (781 / 521 Gh/s
  split); these incremental changes compile cleanly but have not yet been re-run on
  physical hardware.

### Flash it
- **Web flasher** (easiest): `cd ../Flasher && python serve.py` → preset
  **BitAxe (ESP32-S3) factory @0x0** → select `esp-miner-factory.bin` → Connect & Flash.
- **CLI:** `esptool.py --chip esp32s3 -p <PORT> write_flash 0x0 esp-miner-factory.bin`

Then configure dual mining in the web UI (see `../FLASHING_AND_VERIFICATION.md`).

## NerdAxe — prebuilt here too ✅

Built from `NerdAxe-NerdQAxe-ESP-Miner/` (unmodified fork; dual-pool + password are
native). Flash the factory bin at offset **`0x0`**.

| File | Device | How to apply |
|------|--------|--------------|
| `NerdAxe/esp-miner-factory-nerdaxe.bin` | NerdAxe | USB flash @ `0x0` (one-time bootstrap) |
| `NerdAxe/esp-miner-nerdaxe.bin` | NerdAxe | **OTA firmware** — Settings → Manual Update → Firmware |
| `NerdAxe/www.bin` | NerdAxe | **OTA web UI** — Settings → Manual Update → Web UI |

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
```

**Build provenance:** `shufps/esp-idf-builder:0.0.1` (ESP-IDF v5.3.3 + Node 20),
`BOARD=NERDAXE`, secp256k1 submodule @ `0cdc758`. Each build
compiled the firmware **and** its Angular web UI. After flashing, enable dual mining
in the UI (**Pool Mode = Dual** + **Pool Balance** slider) — see
`../NerdAxe-NerdQAxe-ESP-Miner/DUAL_MINING_NOTES.md`.

> Need another board variant (NERDQAXEPLUS2, NERDOCTAXE, gamma, etc.)? The official
> releases cover them: https://github.com/shufps/ESP-Miner-NerdQAxePlus/releases
> — or ask and I'll build that `BOARD=` from source.
