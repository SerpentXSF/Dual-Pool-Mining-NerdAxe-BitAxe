# Maintenance — tracking upstream as a long-term-support fork

**SerpentX Dual Pool Mining** customizes three upstream firmwares. This doc is the
playbook. The goal is **not** to stay at parity with upstream: this is a deliberate
long-term-support fork of the ESP-Miner v2.14.2 base, and we selectively port fixes
rather than chasing releases. Read "Deliberate divergence" below before starting any
rebase work.

## What's actually custom (the footprint)

| Firmware | Custom code? | Notes |
|----------|--------------|-------|
| **BitAxe** (`BitAxe-ESP-Miner/`, bitaxeorg/ESP-Miner) | **Yes** — the only real code | Pool B parallel pipeline |
| **NerdAxe** (`NerdAxe-ESP-Miner/`, shufps fork) | **No** — dual-pool + password are native upstream | Re-vendor = just drop in the new release |

So **only BitAxe needs merge work.** NerdAxe updates are a straight
file swap.

### BitAxe custom footprint
- **Net-new (never conflicts):** `components/dual_pool/` (scheduler, clamps, failover,
  host tests), `main/tasks/stratum_poolb_task.{c,h}`.
- **Edited upstream files (~24 in `main/`, ~1,360 changed lines; NOT all additive):**
  `main/global_state.h`,
  `main/system.c`, `main/main.c`, `main/nvs_config.{c,h}`, `main/Kconfig.projbuild`,
  `main/CMakeLists.txt`, `main/tasks/create_jobs_task.c`, `main/tasks/asic_result_task.c`,
  `components/stratum/stratum_api.{c,h}`, `main/http_server/system_api_json.c`,
  `main/http_server/axe-os/src/app/components/{pool,home}/…`, `version.txt`,
  `generate-version.js`.
- **Biggest divergence: `components/stratum/stratum_api.c`, 625 changed lines vs the
  v2.14.2 base (397 added / 228 removed).** This was previously described as "mostly
  additive"; it is not. But note *what* it is, because it changes what to do about it:
  **almost none of it is dual-pool code.** One hunk accounts for the bulk - a refactor
  splitting the monolithic `STRATUM_V1_parse` into ten validating `parse_*` helpers with
  a bool return. Only ~7 of 351 added lines in that hunk mention pool/dual at all.
  - **Do NOT try to "hoist" this into `components/dual_pool/`.** There is nothing
    dual-pool-shaped left in it to hoist; `stratum_recv_ctx.c` already took that part.
    It is core stratum parsing that we improved, not fork entanglement.
  - The consequence is a **one-time "whose refactor wins"** decision if we ever rebase,
    not ongoing merge pain. Upstream independently refactored the same function in
    #1755 (+372/-225). Our version adds per-parameter validation the v2.14.2 base
    lacked, so it is not obviously the one to discard.
  - It remains the most likely place for a missed upstream fix to hide (upstream churned
    it 397/236 in v2.15.0), which is what the release-watch ritual below is for.

## Deliberate divergence (the standing policy)

**This is a long-term-support fork of the ESP-Miner v2.14.2 base, not a fork chasing
parity.** The product is the simultaneous dual-pool engine plus field-proven stability.
Upstream parity is explicitly *not* a goal. Decided Aug 2026 after reviewing v2.15.0.

Why this is the right trade:

- **Upstream is not competing on this feature.** v2.15.0 added a saved pool *list* with
  failover selection, not simultaneous mining. Our differentiator is intact.
- **The engine is well isolated.** `components/dual_pool/` plus its own task, and its
  deepest hooks sit in files upstream barely touches (`create_jobs_task.c` moved 6 lines
  in v2.15.0; `asic_result_task.c` 6/4).
- **The migration bridge is upstream's, not ours.** Our flat NVS keys are upstream's own
  legacy format and upstream ships `migrate_legacy_pools()` to convert them,
  non-destructively. Deferring costs almost nothing. Snapshot + our Pool B extension:
  `docs/upstream-reference/`.
- **A full rebase today would stack four migrations at once** - ESP-IDF 6.0.2 with the
  MbedTLS 4 / PSA Crypto move, a from-scratch dashboard rewrite on Angular 19 + Tailwind
  (~228 files upstream), re-porting the engine onto a restructured stratum, and the new
  embedded-www build - on hardware that earns 24/7. Not worth it without a forcing
  function.

What we accept by choosing this: the web UI stays PrimeNG, upstream features do not
arrive by merge, and if a rebase is ever forced the bill is larger than it would be
today.

**Revisit triggers** - any one of these reopens the decision:

1. ESP-IDF 5.5 reaches end of life (currently supported into ~2027-2028).
2. A security or correctness fix lands upstream that genuinely cannot be back-ported.
3. Upstream ships native **simultaneous** dual-pool - at which point what is in question
   is the fork's reason to exist, not its config layer.

## Upstream release watch (do this once per upstream release)

The real risk of an LTS fork is not merge pain, it is **silently missing a security or
correctness fix** in code both sides edit. This ritual converts that hazard into a
checklist item. Budget about an hour.

1. **Fetch the new tag** into a scratch clone (never into this tree):
   `git clone --depth 1 --branch vX.Y.Z https://github.com/bitaxeorg/ESP-Miner.git`
   (or `git fetch --depth 1 origin tag vX.Y.Z` in an existing scratch clone).
2. **Diff the watchlist** - the files where our edits and upstream's churn overlap:
   `git diff vOLD..vNEW -- components/stratum main/tasks main/http_server main/nvs_config.c main/global_state.h main/system.c`
   Diff with `--strip-trailing-cr`; this repo is CRLF and upstream is LF, so a raw diff
   reports every line as changed and tells you nothing.
3. **Triage every hunk** into exactly one of:
   - **PORT** - take it (self-contained fix, applies to our base)
   - **SKIP** - deliberately not taking it, with the reason
   - **N/A** - does not apply (IDF 6-only API, `pools[]`-model-only, UI framework rewrite)
4. **Verify before believing the changelog.** Release notes badly overstate what is new
   for a diverged fork: of v2.15.0's headline items, the heap-fragmentation PSRAM work,
   the `/api/theme` auth gate and the websocket broadcast-under-mutex fix were **already
   in this fork**. Diff the actual tree before writing any code.
5. **Record the verdicts in the ledger below**, then build, and hardware-verify on one
   device before the fleet.
6. **Check the fleet afterwards** with `python tools/fleet_check.py --scan 192.168.50 --ws`.
   It exits non-zero if any miner needs attention, so it can gate a release. The `--ws`
   flag is the important one: it verifies frames actually *arrive* on the live websocket,
   not merely that it connects. A connect-only test would have missed the mute-socket bug
   entirely, because the socket was healthy - it just never spoke.

### Ledger

| Upstream | Reviewed | PORTed | SKIPped / N-A | Notes |
|---|---|---|---|---|
| v2.15.0 | 2026-08-25 | #1889 non-blocking UART logging (`log_buffer.c/h`, adopted byte-identical) | **Already had:** #1766 heap-frag PSRAM, #1759 `/api/theme` auth gate, #1803 broadcast-under-mutex (ours is stronger - we validate the fd with `httpd_ws_get_fd_info` against reuse, upstream does not). **Deferred:** #1829 IDF 6.0.2, #1763/#1839 unified firmware, #1651/#1815 Angular+Tailwind, the SV2 cluster. **Do not take as-is:** #1731 duplicate-jobId filter - see the trap note below. | Upstream shipped our websocket handshake bug in v2.14.2 and only fixed it inside their IDF 6 PR; this fork fixed it first (1.3). |

### Known trap: upstream #1731 (duplicate jobId filter)

Upstream dedups jobs against a **flat, global** array of job-ID strings. Stratum job IDs
are **pool-local and collide routinely** - both pools happily issue `"1"`, `"2"`, `"a"`.
Taken as-is into this fork it would silently discard legitimate work from the second
pool, presenting as unexplained hashrate loss with no error anywhere. If it is ever
wanted, the dedup key must be `(pool_index, job_id)` with per-pool arrays cleared on that
pool's own reconnect. Upstream's companion global `reset_extranonce2` flag has the same
defect. Tempting because job re-delivery across a switch may well contribute to our
interval-vs-error behaviour - which is precisely why the naive version would be worse.

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

**Also applied:**
3. ✅ **UI delta isolated** — the dual-mining settings block and the per-pool hashrate card
   now live in their own components (`components/pool/dual-mining-settings.component.ts`,
   `components/home/pool-hashrate.component.ts`). Upstream `pool.component.html` /
   `home.component.html` carry only a one-line `<app-dual-mining-settings>` /
   `<app-pool-hashrate>`. The settings component uses
   `viewProviders: [{ provide: ControlContainer, useExisting: FormGroupDirective }]` so its
   `formControlName` bindings resolve against the parent pool form with no `@Input` plumbing.
4. ✅ **Version string** — set to `SerpentX-DualPool-1.1` in `version.txt`. Note: `version.txt`
   is *our own net-new file* (upstream ESP-Miner has none — it derives the version from
   `git describe`, see the `generate-version.js` fallback), so it never actually conflicts on
   an upstream merge. The earlier `<upstream>-dualpool` idea assumed a shared file; since there
   isn't one, a branded string is fine. Firmware `PROJECT_VER` and the web `axeOSVersion` both
   read this one file, so they always match (no version-mismatch banner). Bump it per release.

`dual_pool/` is clean enough that upstreaming it is *conceivable*, but treat that as
aspirational, not a plan. Upstream v2.15.0 deliberately architected the other way: a
saved pool **list** with `primary_pool_index`/`secondary_pool_index` selected as
failover (`screen.c`: `is_using_fallback ? secondary : primary`), driven by a single
`create_jobs_task`. Simultaneous mining does not fit that shape, and our own field data
(~20% error at a 500 ms split interval vs ~3% at 3000 ms) is exactly the support-burden
argument a maintainer would use to decline. Gauging appetite with a discussion issue is
cheap; plan for "no".

## Known latent issues (from review — none break steady-state mining)

- **Version-mask** — the ASIC rolls versions with Pool A's mask (`ASIC_set_version_mask`
  uses Pool A only). Pool B jobs are now precomputed with the **same mask the chip actually
  rolls** (`create_jobs_task.c` `generate_work`: uses `version_mask`, falling back to
  `version_maskB` only before Pool A negotiates), so midstates always match the chip. A
  startup **warning fires when `version_maskB != version_mask`** — if the two pools disagree
  (virtually never; nearly all use `0x1fffe000`), Pool B can still reject the shares whose
  version bits fall outside its own mask, because the single chip serves both pools.
- **Reconnect-window races (applied)** — `transportB` and `extranonce_strB` were
  use-after-free if a Pool B reconnect coincided with a Pool B slice. Fixed with two pthread
  mutexes (spinlocks can't wrap socket I/O / `strdup`): `transportB_lock` serialises the Pool
  B share submit (`asic_result_task.c` `poolb_submit_share_locked`) against the poolb task's
  close/destroy, and the poolb task now only publishes `transportB` **after** a successful
  connect (via a local handle). `extranonceB_lock` serialises the `extranonce_strB` swap/free
  against `create_jobs` `generate_work`, which now copies the string under the lock and frees
  the copy right after the coinbase hash. Pool A's paths are left byte-for-byte as upstream.
- **Live-tunable** — Split Interval / Pool A share % / dual-enable are now re-read ~1×/sec
  in `create_jobs_task.c` and the scheduler re-inits on change, so tuning them from the web
  UI no longer needs a reboot (they used to latch at boot).
- **Pool-aware `clean_jobs` (applied — the big one)** — both pools share the single 128-slot
  ASIC job ring. `SYSTEM_clean_jobs_queue` (`system.c`) is called only by Pool A, and used to
  invalidate *every* used slot — silently killing Pool B's in-flight job on every Pool A clean
  (~0.5–1 s of Pool B work lost; 1–2% of Pool B output against pools that re-template every
  30–60 s). It now preserves `pool_id == POOL_B` slots; `stratum_poolb_task.c` does the mirror
  on Pool B cleans. The `poolBStaleDrops` counter in `/api/system` tracks residual loss (should
  stay ~0). This is the *at-source* fix; the dropped-share recovery below is now just backstop.
- **A→B slice donation (applied)** — `create_jobs_task.c` now mines Pool B when Pool A has no
  work (boot before first notify) or its socket is down, instead of idling the ASIC or
  generating Pool A work whose shares get dropped. Mirrors the existing B→A donation.
- **Pool B drain-to-newest (applied)** — Pool B now mines the newest queued notify, not the
  oldest, so a long Pool-A-weighted stretch never leaves B on a stale (pool-expired) template.
- **Dropped-share recovery (applied — backstop)** — `asic_result_task.c` re-tests a
  sub-difficulty nonce against other live templates and submits to the pool that owns it. Note
  (per review): near-zero yield in practice — clean-invalidated nonces are dropped upstream in
  `BM1370_process_work` before this runs, and overwritten slots free their template. Harmless;
  the real shares are saved by the pool-aware clean fix above. None of this moves the dashboard
  error % (`REGISTER_ERROR_COUNT` is a chip-health register, independent of share validation).
- **Cosmetic (fixed)** — Pool B no longer counts its authorize reply as an accepted share:
  `stratum_poolb_task.c` skips the `STRATUM_RESULT` whose `message_id` is the authorize uid
  (configure/subscribe return as their own result methods), mirroring the Pool A pattern.

## Build reference

- BitAxe: `espressif/idf:v5.5.3` (Docker), `GITHUB_ACTIONS=true idf.py build` with the web
  UI prebuilt (`npm ci && npm run build` in `main/http_server/axe-os`), then `merge_bin.sh`.
- NerdAxe: `shufps/esp-idf-builder:0.0.1`, `BOARD=NERDAXE`,
  `idf.py build`, then esptool `merge_bin`.
- Host tests: `cd BitAxe-ESP-Miner/components/dual_pool/test_host && make run`.
