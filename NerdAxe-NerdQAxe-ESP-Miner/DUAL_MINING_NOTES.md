# NerdAxe / NerdQAxe — Dual Mining & Pool Password

**Status: both requested features are already implemented natively in this firmware
(the shufps ESP-Miner-NerdQAxePlus fork). No source changes were required.**

This document explains where the functionality lives and how to enable it, so this
tree matches the intent of the Decentralized Dual Miners project alongside the
custom BitAxe build.

## 1. True dual-pool mining (adjustable split) — built in

Implemented by `main/stratum/stratum_manager_dual_pool.cpp`
(`StratumManagerDualPool`):

- Two Stratum connections are held **simultaneously** and both keep mining
  (`reconnectTimerCallback`: "both pools always try to stay alive").
- Work is interleaved by an **error-diffusion balance** in `getNextActivePool()`
  using `m_balance` and `m_error_accum` — the same weighted-slice technique used in
  the BitAxe build. `m_balance` is the Pool A (PRIMARY) share percent; Pool B
  (SECONDARY) gets the remainder.
- Per-pool difficulty handling (`selectAsicDiff`), per-pool accepted/rejected,
  best-diff, ping, and coinbase verification are all tracked separately.

**Enable it:** in the web UI **Settings** page, set **Pool Mode = Dual** and use the
**Pool Balance** slider (1–99 %). Mode/balance persist to NVS via
`Config::getPoolMode()` / `Config::getPoolBalance()`; the manager is chosen at boot in
`main/main.cpp` (`poolMode == 1` → `StratumManagerDualPool`, else
`StratumManagerFallback`).

- 70/30 → balance 70; 50/50 → 50; 25/75 → 25; etc.

> Difference vs. the BitAxe build: the split granularity here is **per job**
> (dithered every `getNextActivePool()` call) rather than a fixed millisecond
> interval. The proportional result is identical; there is no separate "interval ms"
> knob because the ASIC job cadence already provides the slicing.

## 2. Custom pool password — built in

- The Stratum login uses the configured password, not a hardcoded `"x"`:
  `main/stratum/stratum_task.cpp` →
  `authenticate(m_transport, m_config->getUser(), m_config->getPassword())`.
- Per-pool source: `main/stratum/stratum_config.cpp` →
  `Config::getStratumPass()` (Pool A) and `Config::getStratumFallbackPass()` (Pool B).
- Web UI: the **Settings** page has masked **Password** fields for both the primary
  and secondary pools (`stratumPassword`, `fallbackStratumPassword`), with
  show/hide toggles.
- The factory default is still `"x"` (Kconfig `CONFIG_STRATUM_PW`), but any value the
  user enters overrides it and is sent to `mining.authorize`.

## 3. Building the two device targets

Board is selected at build time via the `BOARD` environment variable
(`CMakeLists.txt` → `add_compile_definitions($ENV{BOARD})`):

```bash
# NerdAxe
export BOARD="NERDAXE"
idf.py build flash monitor

# NerdQAxe+
export BOARD="NERDQAXEPLUS"      # or NERDQAXEPLUS2 for the ++ board
idf.py build flash monitor
```

Other supported values: `NERDAXEGAMMA`, `NERDEKO`, `NERDHAXEGAMMA`,
`NERDOCTAXEGAMMA`, `NERDOCTAXEPLUS`, `NERDQX`.

## 4. Known difference from the BitAxe build (per-pool failover in dual mode)

The BitAxe build adds **dedicated failover per active pool** while dual mining
(4 endpoints: A + A-failover, B + B-failover). This fork instead offers **Failover
mode OR Dual mode** (2 endpoints), selected by `poolMode`:

- **Dual mode:** Pool A + Pool B both mine (this is requirement #1). If one drops, it
  keeps retrying itself and the other pool carries the hashrate meanwhile.
- **Failover mode:** primary with a secondary backup (classic failover).

Combining both (dual *and* a dedicated backup for each of the two active pools) is not
supported upstream. If that exact behavior is required here too, it is a substantial
addition to the `StratumManager` class hierarchy and should be scoped separately.

## Summary

For NerdAxe and NerdQAxe, enabling the project's goals is a **configuration** task, not
a code change: **Pool Mode → Dual**, set **Pool Balance**, and enter your **pool
passwords** — all in the Settings page. The source here is the upstream fork,
unmodified, provided so all three device families live in one deliverable.
