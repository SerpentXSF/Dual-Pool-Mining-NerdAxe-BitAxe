# Maintenance — keeping up with upstream

**SerpentX Dual Pool Mining** customizes three upstream firmwares. The goal is to keep
pulling upstream releases and re-applying our features cleanly. This doc is the playbook.

## What's actually custom (the footprint)

| Firmware | Custom code? | Notes |
|----------|--------------|-------|
| **BitAxe** (`BitAxe-ESP-Miner/`, bitaxeorg/ESP-Miner) | **Yes** — the only real code | Pool B parallel pipeline |
| **NerdAxe / NerdQAxe** (`NerdAxe-NerdQAxe-ESP-Miner/`, shufps fork) | **No** — dual-pool + password are native upstream | Re-vendor = just drop in the new release |
| **NerdMiner_v2** | **No** — password native; dual-pool out of scope | Re-vendor as-is |

So **only BitAxe needs merge work.** NerdAxe/NerdQAxe/NerdMiner updates are a straight
file swap.

### BitAxe custom footprint
- **Net-new (never conflicts):** `components/dual_pool/` (scheduler, clamps, failover,
  host tests), `main/tasks/stratum_poolb_task.{c,h}`.
- **Edited upstream files (~15, mostly additive):** `main/global_state.h`,
  `main/system.c`, `main/main.c`, `main/nvs_config.{c,h}`, `main/Kconfig.projbuild`,
  `main/CMakeLists.txt`, `main/tasks/create_jobs_task.c`, `main/tasks/asic_result_task.c`,
  `components/stratum/stratum_api.{c,h}`, `main/http_server/system_api_json.c`,
  `main/http_server/axe-os/src/app/components/{pool,home}/…`, `version.txt`,
  `generate-version.js`.

## Recommended workflow: fork + rebased feature branch

1. **Bootstrap once**
   - Find the exact upstream commit this repo's BitAxe import matches (the pristine
     import is monorepo commit `5fa8adc`; diff it against bitaxeorg tags until clean).
   - Fork `bitaxeorg/ESP-Miner`; create `feature/dual-pool` off that commit; replay the
     custom work (either `git filter-repo --subdirectory-filter BitAxe-ESP-Miner` + rebase
     the A–G phase commits, or apply `git diff --relative=BitAxe-ESP-Miner 5fa8adc..HEAD`
     as 2–3 logical commits: core pipeline / integration points / UI+API).
   - Record the base commit/tag in `BitAxe-ESP-Miner/UPSTREAM_BASE`.
2. **Per upstream release**
   - `git fetch upstream && git rebase --onto <new-tag> <old-base> feature/dual-pool`
   - Expect conflicts in at most: `global_state.h`, `create_jobs_task.c`,
     `asic_result_task.c`, and the Angular files.
   - Run `components/dual_pool/test_host` + a full `idf.py build` after every rebase; do
     one hardware smoke test (shares accepted on **both** pools) before shipping bins.
3. **Re-vendor into this repo**
   - Wipe `BitAxe-ESP-Miner/` and copy the fork branch checkout in (init upstream's
     `libsecp256k1` submodule first — its files are vendored here, see commit `d1007b3`).
   - One commit per sync: `sync bitaxe to upstream vX.Y.Z + dual-pool`.

## Merge-pain reducers

**Applied (BitAxe):**
1. ✅ **Reentrant receive moved** out of `components/stratum/stratum_api.c` into
   `components/dual_pool/stratum_recv_ctx.c` (the component now `REQUIRES tcp_transport`).
   `stratum_api.c` changes almost every upstream release; it's off the conflict surface now.
2. ✅ **Sentinels** — key integration points carry `// DUAL-POOL BEGIN` / `// DUAL-POOL END`
   (`stratum_api.c`, `create_jobs_task.c`, `global_state.h`, `asic_result_task.c`).
   `grep -rn "DUAL-POOL" BitAxe-ESP-Miner` is your rebase inventory. (Remaining touched
   files — `system.c`, `main.c`, `nvs_config.*`, `system_api_json.c`, `Kconfig.projbuild`,
   `CMakeLists.txt`, the Angular UI — are additive and listed above; mark them too as you
   revisit each.)

**Still recommended:**
3. **Isolate the UI delta** into dedicated Angular child components, so upstream
   `pool.component.html` / `home.component.html` carry only a one-line `<app-dual-pool-*>`.
4. **Version string:** ship `<upstream-version>-dualpool` (so users know the base) rather
   than the fixed `DualMiner-1.0` pin, which conflicts every release by design.

The end game: `dual_pool/` is clean enough to eventually PR upstream, which is the
ultimate maintenance strategy.

## Known latent issues (from review — none break steady-state mining)

- **Version-mask** — Pool B computes midstates with `version_maskB`, but the ASIC rolls
  versions with Pool A's mask (`ASIC_set_version_mask` uses Pool A only). Harmless while
  both pools negotiate the standard `0x1fffe000` (virtually all do); a **startup warning
  when `version_maskB != version_mask`** is the recommended guard.
- **Reconnect-window races** — `transportB` (submit after leaving the critical section)
  and `extranonce_strB` (freed while `create_jobs` may hold the pointer) are use-after-free
  under a reconnect coinciding with a Pool B slice. Both **mirror the same patterns
  upstream already has for Pool A**; Pool B reconnects more often so exposure is a bit
  higher. Hardening (refcount / submit-under-mutex) is a good follow-up.
- **Cosmetic** — scheduler ratio/interval latch at boot (settings changes need a restart);
  a stale `current_work_B` can survive ~one slice after `clean_jobs`; Pool B may count its
  authorize reply as +1 accepted share.

## Build reference

- BitAxe: `espressif/idf:v5.5.3` (Docker), `GITHUB_ACTIONS=true idf.py build` with the web
  UI prebuilt (`npm ci && npm run build` in `main/http_server/axe-os`), then `merge_bin.sh`.
- NerdAxe/NerdQAxe: `shufps/esp-idf-builder:0.0.1`, `BOARD=NERDAXE` / `NERDQAXEPLUS`,
  `idf.py build`, then esptool `merge_bin`.
- Host tests: `cd BitAxe-ESP-Miner/components/dual_pool/test_host && make run`.
