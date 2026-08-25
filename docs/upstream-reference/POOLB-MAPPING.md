# Pool B → upstream `pools[]` mapping spec

Written while the bridge is cheap, **not** as a commitment to migrate. This fork
deliberately stays on the v2.14.2 flat-key base (see MAINTENANCE.md → "Deliberate
divergence"). This file exists so that *if* a rebase onto upstream's indexed pool model
ever becomes necessary, the config migration is a spec to implement rather than a
design problem to solve under pressure, against miners that are earning.

## Background

Upstream v2.15.0 replaced the flat pool keys with `pool_N` (JSON per index,
`array_size = MAX_POOLS` = 8) plus `prim_idx` / `sec_idx`. Upstream ships
`migrate_legacy_pools()` which already converts **our exact flat keys** and is
non-destructive (legacy keys are left in place, so a downgrade still works). A verbatim
snapshot lives beside this file, since upstream may delete the shim in a later release.

**What upstream's shim covers:** Pool A (`stratumurl`/`stratumport`/`stratumuser`/
`stratumpass`/…) → `pool_1`, and the Pool A fallback (`fbstratum*`) → `pool_2`, then
`prim_idx = 0`, `sec_idx = 1`.

**What it does NOT cover:** everything below. Pool B is ours alone.

## Mapping

| Our key (`nvs_key_name`) | REST name | Target |
|---|---|---|
| `poolburl`    | `poolBUrl`      | `pool_3`.url |
| `poolbport`   | `poolBPort`     | `pool_3`.port |
| `poolbuser`   | `poolBUser`     | `pool_3`.user |
| `poolbpass`   | `poolBPassword` | `pool_3`.password |
| `poolbtls`    | `poolBTLS`      | `pool_3`.tls |
| `poolbfburl`  | `poolBFallbackUrl`      | `pool_4`.url |
| `poolbfbport` | `poolBFallbackPort`     | `pool_4`.port |
| `poolbfbuser` | `poolBFallbackUser`     | `pool_4`.user |
| `poolbfbpass` | `poolBFallbackPassword` | `pool_4`.password |
| `poolbfbtls`  | `poolBFallbackTLS`      | `pool_4`.tls |

Indices are 1-based to match upstream's `%s_%0*d` key formatter (`pool_1`…`pool_8`), so
Pool B lands at `pool_3` immediately after upstream's own `pool_1`/`pool_2`.

### Scalars that do NOT move

These describe the *splitter*, not a pool, and have no equivalent in upstream's model.
They stay flat regardless:

| Key | REST name | Meaning |
|---|---|---|
| `dualenable` | `dualEnable`     | splitter on/off |
| `dualintms`  | `dualIntervalMs` | split interval, default 3000 (see below) |
| `dualratioa` | `dualRatioA`     | share of hashrate to Pool A, percent |

New index keys the engine would need: something like `poolb_idx` / `poolb_sec_idx`
(defaulting to 2 / 3, i.e. `pool_3` / `pool_4`) so the Pool B task selects from the array
the same way upstream's primary/secondary indices do — rather than hard-coding index 3.

## Non-negotiables for any implementation

1. **Non-destructive.** Follow upstream: write the new keys, never erase the legacy ones.
   A miner must survive a downgrade with its pool config intact.
2. **Idempotent.** Migration runs from a fallback path on missing-key; running it twice
   must not duplicate or clobber.
3. **`dualintms` default must stay 3000.** Field-measured: 500 ms produced ~20% error
   rate, 3000 ms ~3%. A migration that resets this to a lower default silently degrades
   every deployed miner.
4. **Bench first.** Prove it on a device that is not earning, including the
   downgrade-after-migrate path, before it touches the fleet.
5. If `MAX_POOLS` is ever < 4 in a future upstream, this mapping breaks — check it.
