# Decentralized Dual Miners

Customized open-source ESP32 SHA-256 miner firmware adding **true simultaneous
dual-pool mining**, a **custom Pool Password** field, and **per-pool failover** to
the BitAxe / NerdAxe / NerdQAxe device families.

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

## Device → codebase mapping

| Device type | Folder                     | Base firmware                         | Status |
|-------------|----------------------------|---------------------------------------|--------|
| **BitAxe**  | `BitAxe-ESP-Miner/`        | ESP-Miner (bitaxeorg mainline, C)     | ✅ implemented |
| **NerdAxe** | `NerdAxe-NerdQAxe-ESP-Miner/` (planned) | ESP-Miner-NerdQAxePlus fork, board `nerdaxe` | ⏳ planned |
| **NerdQAxe**| `NerdAxe-NerdQAxe-ESP-Miner/` (planned) | ESP-Miner-NerdQAxePlus fork, board `nerdqaxeplus` | ⏳ planned |
| (CPU miner) | `NerdMiner_v2/` (planned)  | BitMaker-hub NerdMiner_v2 (Arduino)   | ⏳ planned |

The NerdAxe and NerdQAxe builds both come from the shufps NerdQAxe fork (it has
dedicated board files for both). NerdMiner_v2 is the CPU "lottery" miner and is the
only target where hashing is literally split across CPU threads.

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
| Split interval (ms) | `dualIntervalMs` | 500 (100–60000) |
| Pool A share % | `dualRatioA` | 50 (0–100) |
| Pool B host/port/user/password/TLS | `poolBUrl` `poolBPort` `poolBUser` `poolBPassword` `poolBTLS` | — |
| Pool B failover host/port/user/password/TLS | `poolBFallbackUrl` `poolBFallbackPort` `poolBFallbackUser` `poolBFallbackPassword` `poolBFallbackTLS` | — |

Pool A's failover reuses the existing **Fallback Pool** configuration.
Passwords are never returned by the GET API (same as stock).

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
   `dualIntervalMs=500`, `dualRatioA=70`.
2. `idf.py monitor` — confirm both `stratum_v1_task` and `stratum poolb` connect and
   stay connected (no disconnect churn).
3. Over ~15 min, confirm accepted shares land on **both** pools, roughly 70/30 by count
   (see `poolBSharesAccepted` / `poolASharesAccepted` in `GET /api/system`, or the pool
   dashboards).
4. Kill Pool B's primary endpoint → confirm Pool B fails over to its failover endpoint
   while Pool A keeps mining. Restore → confirm it returns.
5. Set `dualEnable=false` → confirm behaviour is indistinguishable from stock.

## Notes & limitations

- Pool B uses **Stratum V1**. Pool A retains V1 **and** V2; when Pool A runs V2, Pool B
  (V1) still mines and its shares route correctly.
- Total device hashrate is shared between the pools in the configured ratio.
- This is a customization of third-party open-source firmware; review pool terms before
  pointing hashrate at any pool.
