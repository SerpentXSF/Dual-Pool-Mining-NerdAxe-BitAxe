# SerpentX Dual Pool Mining — BitAxe & NerdAxe

Customized open-source ESP32 SHA-256 miner firmware adding **true simultaneous
dual-pool mining**, a **custom Pool Password** field, and **per-pool failover** to
the BitAxe and NerdAxe device families.

> **Not NerdQAxe.** The NerdQAxe++ is a different device (4× BM1370) with its own
> [NerdQAxe-Quad-Miner](https://github.com/SerpentXSF/NerdQAxe-Quad-Miner) repo and
> native/4-pool dual mining — it is intentionally out of scope here.

> **Maintaining this / taking upstream updates:** see [MAINTENANCE.md](MAINTENANCE.md).
> **Flashing + per-device verification:** see [FLASHING_AND_VERIFICATION.md](FLASHING_AND_VERIFICATION.md).
> **Prebuilt binaries + OTA files:** see [Firmware-Binaries/](Firmware-Binaries/).

> **Hardware reality (read first):** These boards use fixed-function SHA-256d ASICs
> (BM1362/66/68/70). Dual mining **splits the single hashrate** across two pools by
> time-slicing the one ASIC — it does **not** double your hashrate, and both pools
> must be **SHA-256d / Bitcoin-header** pools (BTC, BCH, DGB-SHA256, …). Scrypt,
> Ethash, RandomX, etc. are physically impossible on this silicon. When dual mining
> is **disabled**, behaviour is identical to stock firmware.

## Features

1. **True dual-pool mining** — two permanently-connected Stratum sessions, interleaved
   on a configurable time interval, with the ASIC's hashing **proportionally split**
   between them (e.g. 70/30, 50/50, 25/75). Not a failover backup — both pools stay
   connected and both receive shares simultaneously.
2. **Custom Pool Password** — a user-settable password per pool, stored in NVS and sent
   to `mining.authorize` (replaces any hardcoded `"x"`).
3. **Dedicated per-pool failover** — Pool A and Pool B each have their own failover
   Stratum endpoint and fail over independently, without interrupting the other pool.

## Quick start

> **BitAxe already running AxeOS? Use OTA (step 3), not a full flash.** AxeOS stores the
> board profile (voltage domains, fan map, calibration) in NVS. OTA keeps it — and your
> WiFi and tune. A **full flash wipes it**; our factory image re-provisions it, but OTA is
> the cleaner, lower-risk path. Reserve the full flash below for a **fresh device or recovery**.
> (NerdAxe uses a compile-time board target, so full flash is fine for it.)

- **Fresh device / recovery — hosted [Web Flasher](https://serpentxsf.github.io/Dual-Pool-Mining-NerdAxe-BitAxe/)**
  (Chrome/Edge, nothing to install): open the page, pick your device — **BitAxe**,
  **NerdAxe** (BM1366), or **NerdAxe Gamma** (BM1370) — plug the miner in with a USB-C
  **data** cable, and click **Connect & Flash**. It flashes the factory image straight from
  your browser. *(Re-flashing after we ship an update? Hard-refresh the page — Ctrl+Shift+R —
  so it fetches the latest manifest; the firmware itself is pinned by commit and never stale.)*
- **Offline / advanced — local USB flasher**: `cd "Local Flasher" && python serve.py`
  → opens `http://localhost:8000` in **Chrome/Edge**. Pick your device preset, select the
  factory bin from [Firmware-Binaries/](Firmware-Binaries/) (`…-factory….bin`, offset `0x0`),
  **Connect & Flash**. If a flash fails, hold **BOOT**, tap **RESET**, release **BOOT**.
- **Already running the firmware (any BitAxe/AxeOS build)? Update over the air** — see step 3 (recommended).

> A full flash wipes WiFi — the miner comes back in **setup-AP mode** (`Bitaxe_xxxx` /
> `NerdAxe_xxxx`), not at its old IP. That's expected; rejoin the setup AP to reconnect it. OTA keeps your config.

### 2 · Configure dual mining + Pool Password

| Device | Where | How |
|--------|-------|-----|
| **BitAxe** | Web UI → **Pool** settings → **"Dual Mining (Simultaneous Pool B)"** | Enable Dual Mining, set **Split Interval** (ms) + **Pool A Share %**, fill **Pool B** host/port/user/**Pool Password** (+ optional Pool B Failover). **Split Interval / Pool A Share % / Enable now apply live — no reboot; Pool B endpoint & credentials still need a Restart.** |
| **NerdAxe** | Web UI → **Settings** | Set **Pool Mode = Dual**, adjust the **Pool Balance** slider, enter each pool's **Password**. Save → Restart. (Native — per-pool hashrate split shows on the dashboard.) |

### 3 · Update over the air (OTA)

Already running this firmware? Update without a cable and keep your pool config. In the
device's web UI, open the update page and upload **both** files for your device — the
**firmware** app image *and* the **website** (`www.bin`).

> **The update page checks the file name and rejects anything that doesn't match.**
> Download the firmware file for your **exact model** and **do not rename it**. (This is a
> safety guard — a NerdAxe Gamma won't accept the original NerdAxe image, and vice-versa.)
> The files in [Firmware-Binaries/](Firmware-Binaries/) are already named to match.

| Device | Where | Firmware file (upload as-is) | Website file |
|--------|-------|------------------------------|--------------|
| **BitAxe** | AxeOS → **System / Update** | `esp-miner.bin` | `www.bin` |
| **NerdAxe** (BM1366) | web UI → **Settings** | `esp-miner-nerdaxe.bin` | `www.bin` |
| **NerdAxe Gamma** (BM1370) | web UI → **Settings** | `esp-miner-NerdAxeGamma.bin` | `www.bin` |

Upload **both** every time — the firmware and web UI are a matched pair (a mismatch triggers
a version-mismatch warning in the UI). The device reboots after the firmware upload. The
`esp-miner-factory-*.bin` images are for a **full USB / Web-Flasher** flash at `0x0`, *not* OTA.

Prebuilt firmware, OTA files, and SHA-256 sums: [Firmware-Binaries/](Firmware-Binaries/).
Full step-by-step with verification: [FLASHING_AND_VERIFICATION.md](FLASHING_AND_VERIFICATION.md).

## Device → codebase mapping

| Device type | Folder                     | Base firmware                         | Status |
|-------------|----------------------------|---------------------------------------|--------|
| **BitAxe**  | `BitAxe-ESP-Miner/`        | ESP-Miner (bitaxeorg mainline, C)     | ✅ implemented + **prebuilt bin** in [Firmware-Binaries/](Firmware-Binaries/) |
| **NerdAxe** | `NerdAxe-ESP-Miner/` | ESP-Miner-NerdQAxePlus fork, `BOARD=NERDAXE` | ✅ native + **prebuilt bin** in [Firmware-Binaries/](Firmware-Binaries/) |

The NerdAxe build comes from the shufps NerdQAxePlus fork, which **already implements
dual-pool mining (Pool Mode = Dual + Pool Balance slider) and per-pool custom passwords
natively** — no code changes were required. See
[NerdAxe-ESP-Miner/DUAL_MINING_NOTES.md](NerdAxe-ESP-Miner/DUAL_MINING_NOTES.md).
(That fork can also target NerdQAxe boards, but the NerdQAxe++ is out of scope here — it
has its own [NerdQAxe-Quad-Miner](https://github.com/SerpentXSF/NerdQAxe-Quad-Miner) repo.)

## How dual mining works (BitAxe implementation)

- **Pool A** = the firmware's existing single-pool pipeline, left untouched.
- **Pool B** = a parallel set of fields plus a self-contained second Stratum task
  (`main/tasks/stratum_poolb_task.c`) with its own socket, receive buffer, and
  failover state machine — it does **not** touch the protocol coordinator, so Pool A's
  lifecycle is unchanged.
- **Scheduler** (`components/dual_pool/pool_scheduler.c`) — a weighted error-diffusion
  time-slicer. Each slice of length *Split Interval* is assigned to Pool A or Pool B so
  the long-run ratio matches *Pool A Share %*. `create_jobs_task` consults it and stamps
  each `bm_job` with its `pool_id`.
- **Result routing** — `asic_result_task` reads `bm_job.pool_id` and submits each found
  share to the originating pool's socket, with that pool's credentials and extranonce.
  **Dropped-share recovery:** if a rapid switch reuses a job slot before its nonce returns,
  a below-difficulty nonce is re-tested against the other live templates and still submitted
  to the pool that actually owns it, instead of being lost.
- **Failover** (`components/dual_pool/pool_failover.c`) — per-pool
  `primary → failover → donate-slices-to-other-pool` state machine.

The pure logic (scheduler / clamps / failover) lives in the `dual_pool` component and
has host-compilable unit tests (`components/dual_pool/test_host/`, plain `gcc`).

## New configuration fields

Set via the web portal (**Pool** settings → *Dual Mining (Simultaneous Pool B)*), or
via REST (`PATCH /api/system`). NVS `rest_name` keys:

| Field | Key | Default |
|-------|-----|---------|
| Enable dual mining | `dualEnable` | false |
| Split interval (ms) | `dualIntervalMs` | 3000 (100–60000) |
| Pool A share % | `dualRatioA` | 50 (0–100) |
| Pool B host/port/user/password/TLS | `poolBUrl` `poolBPort` `poolBUser` `poolBPassword` `poolBTLS` | — |
| Pool B failover host/port/user/password/TLS | `poolBFallbackUrl` `poolBFallbackPort` `poolBFallbackUser` `poolBFallbackPassword` `poolBFallbackTLS` | — |

Pool A's failover reuses the existing **Fallback Pool** configuration.
Passwords are never returned by the GET API (same as stock).
`dualEnable`, `dualIntervalMs`, and `dualRatioA` are **re-read live (~1×/sec)** so tuning
them applies without a reboot; the Pool B endpoint/credential fields load at boot (Restart).

## Building (BitAxe)

Requires the ESP-IDF toolchain and Node/npm (for the web UI), same as upstream ESP-Miner.

```bash
cd BitAxe-ESP-Miner
idf.py set-target esp32s3      # per your board
idf.py build
idf.py -p <PORT> flash monitor
```

Board model (Supra/Gamma/etc.) is selected at flash/config time via the
`config-*.cvs` files / `board_version` NVS key, exactly as in stock ESP-Miner.

### Run the pure-logic host tests (no hardware needed)

```bash
cd BitAxe-ESP-Miner/components/dual_pool/test_host
make run        # requires gcc + make
```

Validates scheduler ratio accuracy, clamps, and the failover state machine.

## Verifying dual mining

1. Configure two SHA-256d Stratum endpoints as Pool A and Pool B; set `dualEnable=true`,
   `dualIntervalMs=3000`, `dualRatioA=70`.
2. `idf.py monitor` — confirm both `stratum_v1_task` and `stratum poolb` connect and
   stay connected (no disconnect churn).
3. Over ~15 min, confirm accepted shares land on **both** pools, roughly 70/30 by count
   (see `poolBSharesAccepted` / `poolASharesAccepted` in `GET /api/system`, or the pool
   dashboards).
4. Kill Pool B's primary endpoint → confirm Pool B fails over to its failover endpoint
   while Pool A keeps mining. Restore → confirm it returns.
5. Set `dualEnable=false` → confirm behaviour is indistinguishable from stock.

## Tuning: switch speed & error rate

- **Split Interval** sets how often the ASIC switches between Pool A and Pool B. **The
  default is 3000 ms**, a field-tested sweet spot: on one miner, going from 500 ms to
  3000 ms took the dashboard error rate from **~20% down to ~3%**. Lower values switch
  more often and drive the ASIC's hardware error counter up via cross-pool work churn —
  **500 ms is too aggressive.** If a miner *still* shows a high error rate at 3000 ms,
  that unit is genuinely thermal/overclock-bound (see next bullet). Changes take effect
  **within ~1 second, with no reboot.**
- **What the dashboard "error rate" actually is:** it's read from the **ASIC's own
  hardware error counter** — a chip-health signal (nonces the silicon itself rejects),
  *not* a count of dual-pool share problems. Slowing the Split Interval reduces cross-pool
  work churn, but a **persistently high error rate on a specific unit is almost always
  overclock / voltage / cooling** on that chip — lower its frequency or improve airflow.
  (A healthy BitAxe typically sits around a low single-digit %.)
- Both pools should negotiate the standard version mask (`0x1fffe000`); nearly all do. If
  two pools disagree, the firmware logs a one-time warning and Pool B may reject the
  occasional share whose version bits fall outside its mask.
- **Pool problem vs. local problem (how to tell):** a genuinely bad pool shows up as
  climbing **rejected shares** (per-pool via `poolAShares*` / `poolBShares*` in
  `GET /api/system`). If instead **rejects stay flat while the error rate rises**, the loss
  is **local, at the switch boundary**: right after a switch (or a `clean_jobs`) the ASIC is
  still returning nonces computed against the *previous* pool's template — validated against
  the new template they fail and would be thrown away, even though they're valid shares for
  the pool that issued them. The firmware's **dropped-share recovery** (see *How dual mining
  works*) catches these: a failing nonce is re-tested against every live template and
  submitted to the pool it actually matches, instead of being discarded. (Because the
  *dashboard* error % is the ASIC hardware counter — previous bullet — these boundary
  discards surface mainly as **fewer accepted shares**, not as that percentage.)

## Notes & limitations

- Pool B uses **Stratum V1**. Pool A retains V1 **and** V2; when Pool A runs V2, Pool B
  (V1) still mines and its shares route correctly.
- Total device hashrate is shared between the pools in the configured ratio.
- This is a customization of third-party open-source firmware; review pool terms before
  pointing hashrate at any pool.

## License & Credits

GPL-3.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).

This repository is a derivative work combining two upstream GPL-3.0 firmwares,
each preserved with its original license in its subdirectory:

- **BitAxe** — [bitaxeorg/ESP-Miner](https://github.com/bitaxeorg/ESP-Miner)
  (AxeOS), GPL-3.0 — see [BitAxe-ESP-Miner/LICENSE](BitAxe-ESP-Miner/LICENSE)
- **NerdAxe** — [shufps/ESP-Miner-NerdQAxePlus](https://github.com/shufps/ESP-Miner-NerdQAxePlus),
  GPL-3.0 — see [NerdAxe-ESP-Miner/LICENSE](NerdAxe-ESP-Miner/LICENSE)

SerpentX modifications (simultaneous dual-pool split, per-pool password, per-pool
failover, prebuilt binaries + web flasher, in-UI update panel) are GPL-3.0; see
the git commit history for the changes and their dates. Bundled third-party
components (e.g. `libsecp256k1`, MIT) keep their own licenses. Full attribution
and statement of changes: [NOTICE](NOTICE).

"BitAxe", "NerdAxe", and "NerdQAxe" are names of their respective upstream
projects; this is an independent, community-maintained fork and is not affiliated
with or endorsed by them.
