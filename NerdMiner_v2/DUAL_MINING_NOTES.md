# NerdMiner_v2 — Dual Mining & Pool Password

This is the BitMaker-hub NerdMiner_v2 — an ESP32 **CPU "lottery" miner** that hashes
SHA-256 in software (on the order of a few KH/s), not an ASIC. It is included so all
requested devices live in one deliverable.

## Requirement #2 — Custom pool password: already supported ✅

No change needed. The WiFiManager configuration portal already exposes a **"Pool
password"** field and wires it end-to-end:

- `src/wManager.cpp` — `WiFiManagerParameter password_text_box("Poolpassword - Optional",
  "Pool password", Settings.PoolPassword, 80)`. On save (all portal paths) it does
  `strncpy(Settings.PoolPassword, password_text_box.getValue(), …)`.
- `Settings.PoolPassword` is persisted with the rest of the config
  (`SDCrd.loadConfigFile` / `saveConfigFile`).
- `src/mining.cpp` (in `runStratumWorker`) — `strcpy(mWorker.wPass, Settings.PoolPassword);`
  then `tx_mining_auth(client, mWorker.wName, mWorker.wPass)`.
- `src/stratum.cpp` — `tx_mining_auth` sends `mining.authorize` with the user and
  password params. There is **no hardcoded `"x"`** in the auth path; the field simply
  defaults to empty/optional and uses whatever the user enters.

**To use:** open the NerdMiner Wi-Fi config portal and fill in the **Pool password**
field. Done.

## Requirement #1 — True dual-pool mining: intentionally not implemented here

**Decision (agreed):** skip the dual-pool refactor for NerdMiner_v2.

Rationale:

- **Marginal value.** NerdMiner_v2 is a lottery miner hashing a few KH/s in the CPU.
  "Dual mining" would split that already-negligible hashrate across two pools — it does
  not improve expected earnings in any practical way; it is a novelty at this hashrate.
- **Large, invasive change.** The miner is heavily global-state-coupled: a single
  `static WiFiClient client` plus globals `mMiner` / `mJob` / `mWorker` /
  `isMinerSuscribed` shared between one `runStratumWorker` task and two hashing threads
  (`minerWorkerHw` on core 0, `minerWorkerSw` on core 1). True dual-pool requires
  duplicating all of that for a Pool B (second client + stratum worker + job/worker
  state) and re-pointing one hashing thread at each pool, plus per-pool share routing.
  That is comparable in size to the full BitAxe implementation, for near-zero benefit.

### If you ever do want it

The natural design (matching "split the local hashing threads between two pools") is:

- Add Pool B fields (address/port/user/password) to the WiFiManager portal + `Settings`.
- Add a second `WiFiClient clientB` and a second `runStratumWorker` for Pool B.
- Assign `minerWorkerHw` → Pool A job state, `minerWorkerSw` → Pool B job state
  (a fixed 50/50 split, since there are exactly two hashing threads).
- Route found shares to the originating pool's client.

This is scoped as a separate effort. For **BitAxe** (ASIC) the full dual-pool
implementation lives in `../BitAxe-ESP-Miner/`; for **NerdAxe/NerdQAxe** it is native
(see `../NerdAxe-NerdQAxe-ESP-Miner/DUAL_MINING_NOTES.md`).

## Building

Standard NerdMiner_v2 PlatformIO build — select your board's environment in
`platformio.ini` and flash. See the upstream `README.md` in this folder.
