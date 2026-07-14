# Decentralized Dual Miners — Design Spec

**Date:** 2026-07-13
**Status:** Approved design (pending written-spec review)

## 1. Goal

Customize the open-source ESP32 SHA-256 miner firmwares to add:

1. **True dual-pool mining** — two active, permanently-connected Stratum sessions
   whose work is interleaved on a configurable time interval, with the device's
   hashing proportionally split between the two pools. Not a failover backup —
   both sockets stay open and both receive shares simultaneously (time-division).
2. **Custom pool password field** — a user-settable "Pool Password" persisted in
   NVS and passed to the Stratum `mining.authorize` call, for both pools.
3. **Dual Pool Failover** — each active pool (A and B) has its own dedicated
   failover stratum and fails over independently if its primary drops, without
   interrupting the other pool.

Final builds must target three device types: **NerdAxe**, **BitAxe**, **NerdQAxe**.

## 2. Source → device-type → codebase mapping

| Device type | Codebase (modified)                    | Language     | Board selection            |
|-------------|----------------------------------------|--------------|----------------------------|
| BitAxe      | ESP-Miner (bitaxeorg mainline)         | ESP-IDF / C  | runtime `board_version` / `config-*.cvs` |
| NerdAxe     | ESP-Miner-NerdQAxePlus (shufps fork)   | ESP-IDF / C++| build target `nerdaxe`     |
| NerdQAxe    | ESP-Miner-NerdQAxePlus (shufps fork)   | ESP-IDF / C++| build target `nerdqaxeplus` / `nerdqaxeplus2` |
| (CPU miner) | NerdMiner_v2 (BitMaker-hub)            | Arduino/PIO  | PlatformIO env per board   |

The NerdQAxe fork contains dedicated board files for **both** NerdAxe
(`main/boards/nerdaxe.cpp`) and NerdQAxe (`main/boards/nerdqaxeplus.cpp`), so it
produces the NerdAxe and NerdQAxe device builds. NerdMiner_v2 is the CPU
"lottery" miner line (included because it was explicitly selected); it is not one
of the three ASIC device types but receives the same two features.

## 3. Hardware reality (non-negotiable constraints)

- ASIC devices (BitAxe/NerdAxe/NerdQAxe) have **one** hashing engine. Dual mining
  **splits** the single hashrate across two pools; it does not double it.
- **The split ratio is a user control**, not fixed. `dualRatioA` (Pool A share %,
  0–100) sets any proportion — 70/30, 50/50, 25/75, 40/60 — and Pool B gets the
  remainder. See §4.3 and §5.
- Both pools must run **SHA-256d / Bitcoin block-header protocol** coins (BTC, BCH,
  and other SHA-256d chains). The header format is fixed by the ASIC.
- When dual mode is **disabled**, behavior is byte-for-byte identical to stock.

## 4. Shared architecture

### 4.1 `PoolContext`
A struct bundling everything currently held as single-pool state:
`{ transport/socket, own job queue, extranonce1, extranonce2_len, difficulty,
version_mask, connection flags, per-pool share counters }`, plus **two endpoint
records** — `primary` and `failover` — each holding `{ url, port, user, pass, tls }`,
and an `active_endpoint` selector (PRIMARY | FAILOVER) driven by the state machine
in §4.5. Two instances exist: **Pool A** (existing primary; its failover reuses the
firmware's existing fallback-pool config) and **Pool B** (new primary + new failover
block).

**Realization (approved):** `PoolContext` is realized as **parallel Pool-B fields**,
not a full array refactor. Pool A = the firmware's existing single-pool globals,
left untouched; Pool B = a new parallel set of fields + a second stratum task + the
scheduler. This guarantees dual-**off** behavior is byte-identical to stock and
keeps the blast radius small. (The pure scheduler, clamp, and failover-state logic
still live in a self-contained, host-testable module.)

**Build/verify environment note:** modified source + host-compilable logic tests
(scheduler/clamp/failover) are produced here; `idf.py build`/flash and real-pool
mining verification run in the user's ESP-IDF environment on a physical device.

### 4.2 Two stratum tasks
One stratum task per `PoolContext`, each owning a live socket that is never closed
while dual mode is on. Each parses `mining.notify` / `set_difficulty` / extranonce
independently into its own queue and updates its own `PoolContext`.

### 4.3 Weighted time-slice scheduler (job-creation path)
Time is divided into slices of length **Interval** (default 500 ms). Each slice is
assigned to Pool A or Pool B by a Bresenham/error-diffusion accumulator so that,
over a rolling cycle, the A:B slice ratio matches **Pool A share %** (default 50).
During a slice, only that pool's queued work is fed to the ASIC/CPU. This honors
both knobs exactly: **Interval = switch granularity**, **Ratio = proportion**.

Semantics, in two levels:

- **Per slice (every `Interval` ms):** decide which pool owns the *next* slice using
  an error-diffusion accumulator, so the long-run share matches `ratioA`:
  ```
  on slice boundary:
      acc += ratioA                  // ratioA in 0..100
      if (acc >= 100) { active_pool = POOL_A; acc -= 100; }
      else            { active_pool = POOL_B; }
  ```
  Example: ratioA=50 → A,B,A,B…; ratioA=75 → A,A,A,B,A,A,A,B…; ratioA=100 → always A.
- **Per job-send (within a slice):** always feed `active_pool`'s queued work, and
  stamp `job.pool_id = active_pool`. No per-job pool decision — the slice already
  fixed it. If `active_pool`'s socket is down, borrow the other pool for that slice
  (degradation, §7).

### 4.4 Pool-tagged jobs + routed results
Every job carries `pool_id`. On a found nonce, the result path reads
`job->pool_id` and submits the share to **that** pool's socket, using that pool's
extranonce/difficulty context.
- **ESP-Miner / NerdQAxe:** `bm_job` gains a `pool_id`; the existing
  `active_jobs[128]` job table (indexed by ASIC job id) already lets the result
  task recover the originating job → its pool. Submit via `pool_id`'s transport.
- **NerdMiner_v2:** the work item gains a pool tag; the found share is written to
  the matching `WiFiClient`.

### 4.5 Per-pool failover state machine (Dual Pool Failover)
Each `PoolContext` runs an independent connection state machine over its two
endpoints, so Pool A and Pool B fail over **separately** (e.g. A → a BTC backup,
B → a BCH backup):

```
state PRIMARY_OK      -> mining on primary
  on disconnect       -> PRIMARY_RETRY
state PRIMARY_RETRY   -> reconnect primary with backoff, up to N attempts
  on success          -> PRIMARY_OK
  on N failures       -> FAILOVER (if failover endpoint configured) else DOWN
state FAILOVER        -> mining on failover endpoint; keep probing primary
  on primary recovers -> PRIMARY_OK (switch back)
  on failover drops   -> FAILOVER_RETRY -> (primary or failover) or DOWN
state DOWN            -> this pool has no reachable endpoint
```

Precedence for that pool's time slices: **primary → failover → donate to the other
active pool** (last resort, §7). A pool only stops contributing entirely when both
its endpoints are DOWN. Each endpoint carries its own credentials, so a failover
pool with different user/password authorizes correctly.

## 5. Configuration & NVS (new dedicated Pool B block)

New keys (named per each firmware's convention), exposed in the config
portal/REST and persisted to NVS:

| Concept            | Key (example)   | Type | Default | Notes                    |
|--------------------|-----------------|------|---------|--------------------------|
| Dual enable        | `dualEnable`    | bool | false   | master toggle            |
| Slice interval     | `dualIntervalMs`| u16  | 500     | clamp ≥ 100 ms           |
| Pool A share %     | `dualRatioA`    | u8   | 50      | clamp 0..100             |
| Pool B URL         | `poolBUrl`      | str  | ""      |                          |
| Pool B port        | `poolBPort`     | u16  | 3333    |                          |
| Pool B user        | `poolBUser`     | str  | ""      |                          |
| Pool B password    | `poolBPass`     | str  | ""      | → Pool B authorize       |
| Pool B TLS         | `poolBTls`      | bool | false   | where firmware supports  |
| Pool B failover URL| `poolBFbUrl`    | str  | ""      | Pool B backup stratum    |
| Pool B failover port| `poolBFbPort`  | u16  | 3333    |                          |
| Pool B failover user| `poolBFbUser`  | str  | ""      |                          |
| Pool B failover pass| `poolBFbPass`  | str  | ""      | → Pool B failover auth   |
| Pool B failover TLS | `poolBFbTls`   | bool | false   | where firmware supports  |

**Pool A failover** reuses each firmware's **existing** fallback-pool config
(`fallback*` / `getStratumFallback*` / `Settings.Pool2*`) — no new keys; those
fields are simply relabeled in the portal as "Pool A failover". Pool B gets the
new `poolBFb*` block above. This gives dedicated, independent failover per pool.

## 6. Pool Password (requirement #2)

All three firmwares already read a pool password from NVS and pass it to
`mining.authorize` (ESP-Miner `NVS_CONFIG_STRATUM_PASS` → `stratum_v1_task.c`;
NerdQAxe `Config::getStratumPass()`; NerdMiner_v2 `Settings.PoolPassword` →
`tx_mining_auth`). Work reduces to:
- Verify each authorize path uses the stored value (no residual hardcoded `"x"`).
- Ensure a clearly-labeled **"Pool Password"** field is exposed in each config
  portal for **Pool A** and the new **Pool B**.
- Wire `poolBPass` into Pool B's authorize call the same way.

## 7. Error handling / degradation

- A pool's primary socket drops → that pool's state machine (§4.5) reconnects with
  backoff, then fails over to its own failover endpoint, continuing to mine its
  configured ratio. Pool A and Pool B fail over independently.
- A pool has **both** endpoints down → its slices are donated to the other active
  pool until at least one of its endpoints recovers (last-resort degradation).
- **All four** endpoints down → existing "pools unavailable" pause path.
- `dualEnable = false` → second task idle; scheduler feeds only Pool A, and Pool A
  still uses its primary→failover machine = stock single-pool + failover behavior.
- Ratio/interval out of range → clamped (interval ≥ ~100 ms, ratio 0..100).

## 8. Testing / verification

- **Scheduler:** ratio accuracy over N slices (e.g. 70/30 across 1000 slices
  within tolerance); interval timing.
- **Routing:** synthetic found-share → asserts submission on the correct socket
  with the correct extranonce/difficulty context.
- **NVS:** save/load round-trip of all new keys and defaults.
- **Build:** each firmware compiles under its toolchain — ESP-IDF build for
  ESP-Miner and NerdQAxe fork (per board target), PlatformIO build for NerdMiner_v2.
- **Behavioral:** dual-off diff == stock; dual-on against two test Stratum
  endpoints shows accepted shares on both pools in the configured ratio.

## 9. Deliverable layout

```
Decentralized Dual Miners/
  docs/specs/          this spec + implementation plan
  BitAxe-ESP-Miner/                modified ESP-Miner (bitaxeorg) full source
  NerdAxe-NerdQAxe-ESP-Miner/      modified NerdQAxe fork (builds NerdAxe + NerdQAxe)
  NerdMiner_v2/                    modified CPU miner full source
  README.md                        new fields, split model, build steps per device,
                                   SHA-256d + hashrate-split caveat
```

## 10. Out of scope

- Non-SHA-256 algorithms — the ASIC silicon physically implements only double
  SHA-256; Scrypt/Ethash/RandomX/etc. are impossible on these chips regardless of
  firmware. (NerdMiner_v2's CPU path could run other algos in software but only at
  novelty/lottery speed; not in scope.)
- Increasing total hashrate (physically impossible on one ASIC).
- Web dashboard redesign beyond adding the new fields and per-pool share counts.

Note: the existing fallback-pool config is **not** discarded — it is repurposed as
Pool A's dedicated failover (§4.5, §5), so failover semantics are extended, not
removed.
