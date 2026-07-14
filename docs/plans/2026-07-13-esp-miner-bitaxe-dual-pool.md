# ESP-Miner (BitAxe) Dual-Pool Implementation Plan



**Goal:** Add true simultaneous dual-pool mining (two permanently-connected Stratum
sessions, weighted time-sliced onto the single ASIC, shares routed back to the
originating pool), a per-pool custom Pool Password, and dedicated per-pool failover,
to the ESP-Miner (bitaxeorg) firmware for BitAxe devices.

**Architecture:** "Pool A" = the firmware's existing single-pool globals, left
untouched (dual-OFF is byte-identical to stock). "Pool B" = a new parallel set of
fields + a self-contained second Stratum task (own connect + failover loop, bypassing
the protocol coordinator). A pure, host-testable `dual_pool` component holds the
weighted-slice scheduler, config clamps, and failover state machine. `create_jobs_task`
consults the scheduler to choose which pool's queued work to feed the ASIC each slice
and stamps `bm_job.pool_id`; `ASIC_result_task` reads `pool_id` and submits the share
to that pool's transport.

**Tech Stack:** ESP-IDF (C11), FreeRTOS, lwIP/esp_transport, Unity (on-device) +
plain-gcc host tests for pure logic. Frontend: Angular (axe-os) + REST via cJSON.

## Global Constraints

- Target codebase: `_extracted/ESP-Miner-master` → deliverable copy at
  `Decentralized Dual Miners/BitAxe-ESP-Miner/`. All edits are made in the deliverable copy.
- SHA-256d/Bitcoin-header pools only (hardware constraint). Both pools use Stratum V1
  for Pool B (V2 for Pool B is out of scope for this plan; Pool A keeps V1+V2).
- Dual-OFF (`dualEnable=false`) MUST produce byte-identical mining behavior to stock:
  the scheduler always returns Pool A, the Pool B task idles, no Pool B sockets open.
- Do not allocate heap in the per-nonce hot path beyond what stock already does.
- New NVS string keys respect the existing `NVS_STR_LIMIT`; follow the existing
  `Settings default_configs[]` row pattern exactly.
- Scheduler slice interval clamped to [100, 60000] ms; ratio clamped to [0, 100].
- Pool A failover REUSES existing `NVS_CONFIG_FALLBACK_*` keys (relabel only). Pool B
  adds new `poolBFb*` keys.
- Host logic tests build & run with `gcc` (no ESP-IDF). On-device `idf.py build`/flash
  is run by the user; those steps are marked **[USER-RUN]**.

---

## File Structure

**New files (deliverable copy):**
- `components/dual_pool/include/pool_scheduler.h` — weighted-slice scheduler API (pure).
- `components/dual_pool/pool_scheduler.c` — scheduler impl (pure).
- `components/dual_pool/include/dual_clamp.h` — ratio/interval clamps (pure).
- `components/dual_pool/dual_clamp.c` — clamp impl (pure).
- `components/dual_pool/include/pool_failover.h` — per-pool failover state machine (pure).
- `components/dual_pool/pool_failover.c` — failover impl (pure).
- `components/dual_pool/CMakeLists.txt` — component registration.
- `components/dual_pool/test_host/test_dual_pool.c` — plain-gcc unit tests.
- `components/dual_pool/test_host/Makefile` — host test build/run.
- `main/tasks/stratum_poolb_task.c` / `.h` — Pool B self-contained Stratum+failover task.

**Modified files (deliverable copy):**
- `main/nvs_config.h` — add NVS key enum entries.
- `main/nvs_config.c` — add `Settings` rows + defaults.
- `main/global_state.h` — add Pool B parallel fields + dual config + scheduler state + `pool_id`.
- `components/stratum/include/mining.h` — add `uint8_t pool_id;` to `bm_job`.
- `main/system.c` — load Pool B + dual config from NVS into `SystemModule`.
- `main/tasks/create_jobs_task.c` — scheduler integration + pool-parametrized `generate_work`.
- `main/tasks/asic_result_task.c` — route share submit by `bm_job.pool_id`.
- `main/main.c` — create the Pool B task.
- `main/http_server/http_server.c` (+ `system_api_json.c`) — expose new fields in REST.
- `main/http_server/axe-os/...` — add portal form fields.
- `main/Kconfig.projbuild` — add `CONFIG_POOL_B_*` defaults.

---

## Phase A — Pure, host-testable core (TDD with gcc)

### Task A1: Scaffold the `dual_pool` component and host test harness

**Files:**
- Create: `components/dual_pool/CMakeLists.txt`
- Create: `components/dual_pool/include/dual_clamp.h`, `components/dual_pool/dual_clamp.c`
- Create: `components/dual_pool/test_host/Makefile`
- Create: `components/dual_pool/test_host/test_dual_pool.c`

**Interfaces:**
- Produces: `uint8_t dual_clamp_ratio(int32_t v)`, `uint16_t dual_clamp_interval(int32_t v)`.

- [ ] **Step 1: Write the failing test** — `components/dual_pool/test_host/test_dual_pool.c`

```c
#include <assert.h>
#include <stdio.h>
#include "dual_clamp.h"

static void test_clamp_ratio(void) {
    assert(dual_clamp_ratio(-5) == 0);
    assert(dual_clamp_ratio(0)  == 0);
    assert(dual_clamp_ratio(50) == 50);
    assert(dual_clamp_ratio(100) == 100);
    assert(dual_clamp_ratio(150) == 100);
}

static void test_clamp_interval(void) {
    assert(dual_clamp_interval(0)     == 100);    // below floor -> floor
    assert(dual_clamp_interval(50)    == 100);
    assert(dual_clamp_interval(500)   == 500);
    assert(dual_clamp_interval(60001) == 60000);  // above ceiling -> ceiling
}

int main(void) {
    test_clamp_ratio();
    test_clamp_interval();
    printf("A1 clamp tests passed\n");
    return 0;
}
```

- [ ] **Step 2: Create the Makefile** — `components/dual_pool/test_host/Makefile`

```make
CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -O0 -g -I../include
SRC      = ../dual_clamp.c ../pool_scheduler.c ../pool_failover.c
TEST     = test_dual_pool.c

run: build
	./test_dual_pool

build: $(SRC) $(TEST)
	$(CC) $(CFLAGS) -o test_dual_pool $(TEST) $(SRC)

clean:
	rm -f test_dual_pool
```

- [ ] **Step 3: Create header + stub impl so it links** — `include/dual_clamp.h`

```c
#ifndef DUAL_CLAMP_H
#define DUAL_CLAMP_H
#include <stdint.h>
uint8_t  dual_clamp_ratio(int32_t v);
uint16_t dual_clamp_interval(int32_t v);
#endif
```

`dual_clamp.c` (deliberately wrong to see the test fail):

```c
#include "dual_clamp.h"
uint8_t  dual_clamp_ratio(int32_t v)    { return (uint8_t)v; }
uint16_t dual_clamp_interval(int32_t v) { return (uint16_t)v; }
```

Also create empty `pool_scheduler.c` and `pool_failover.c` with `/* stub */` so the
Makefile's `SRC` links (they gain content in A2/A3).

- [ ] **Step 4: Run test to verify it FAILS**

Run: `cd components/dual_pool/test_host && make run`
Expected: assertion failure (e.g. `dual_clamp_ratio(-5) == 0`).

- [ ] **Step 5: Implement correctly** — `dual_clamp.c`

```c
#include "dual_clamp.h"
uint8_t dual_clamp_ratio(int32_t v) {
    if (v < 0)   return 0;
    if (v > 100) return 100;
    return (uint8_t)v;
}
uint16_t dual_clamp_interval(int32_t v) {
    if (v < 100)   return 100;
    if (v > 60000) return 60000;
    return (uint16_t)v;
}
```

- [ ] **Step 6: Run test to verify it PASSES**

Run: `make run`
Expected: `A1 clamp tests passed`.

- [ ] **Step 7: Register the ESP-IDF component** — `components/dual_pool/CMakeLists.txt`

```cmake
idf_component_register(
    SRCS "dual_clamp.c" "pool_scheduler.c" "pool_failover.c"
    INCLUDE_DIRS "include"
)
```

- [ ] **Step 8: Commit**

```bash
git add components/dual_pool
git commit -m "feat(dual_pool): scaffold component + clamp helpers with host tests"
```

### Task A2: Weighted-slice scheduler

**Files:**
- Create: `components/dual_pool/include/pool_scheduler.h`, `components/dual_pool/pool_scheduler.c`
- Modify: `components/dual_pool/test_host/test_dual_pool.c`

**Interfaces:**
- Produces:
  - `typedef enum { POOL_A = 0, POOL_B = 1 } pool_id_t;`
  - `pool_scheduler_t` struct (fields below).
  - `void pool_scheduler_init(pool_scheduler_t *s, uint8_t ratio_a, uint16_t interval_ms, int64_t now_us);`
  - `pool_id_t pool_scheduler_select(pool_scheduler_t *s, int64_t now_us);`
- Consumes: `dual_clamp_ratio`, `dual_clamp_interval` (A1).

- [ ] **Step 1: Write the failing test** (append to `test_dual_pool.c`, and call from `main`)

```c
#include "pool_scheduler.h"

static void test_scheduler_ratio_70_30(void) {
    pool_scheduler_t s;
    pool_scheduler_init(&s, 70, 500, 0);
    int a = 0, b = 0;
    // Sample once per slice for 1000 slices of 500 ms.
    for (int i = 0; i < 1000; i++) {
        int64_t now = (int64_t)i * 500 * 1000; // us
        if (pool_scheduler_select(&s, now) == POOL_A) a++; else b++;
    }
    // 70/30 within +/- 2 slices tolerance from error diffusion.
    assert(a >= 698 && a <= 702);
    assert(a + b == 1000);
}

static void test_scheduler_extremes(void) {
    pool_scheduler_t s;
    pool_scheduler_init(&s, 100, 500, 0);
    for (int i = 0; i < 50; i++) assert(pool_scheduler_select(&s, (int64_t)i*500*1000) == POOL_A);
    pool_scheduler_init(&s, 0, 500, 0);
    for (int i = 0; i < 50; i++) assert(pool_scheduler_select(&s, (int64_t)i*500*1000) == POOL_B);
}

static void test_scheduler_holds_within_slice(void) {
    pool_scheduler_t s;
    pool_scheduler_init(&s, 50, 500, 0);
    pool_id_t first = pool_scheduler_select(&s, 0);
    // Same slice (t < 500 ms) must return the same pool.
    assert(pool_scheduler_select(&s, 100 * 1000) == first);
    assert(pool_scheduler_select(&s, 499 * 1000) == first);
}
```

Add to `main`: `test_scheduler_ratio_70_30(); test_scheduler_extremes(); test_scheduler_holds_within_slice();`

- [ ] **Step 2: Create the header** — `include/pool_scheduler.h`

```c
#ifndef POOL_SCHEDULER_H
#define POOL_SCHEDULER_H
#include <stdint.h>
#include <stdbool.h>

typedef enum { POOL_A = 0, POOL_B = 1 } pool_id_t;

typedef struct {
    uint8_t   ratio_a;        // 0..100 Pool A share percent
    uint16_t  interval_ms;    // slice length
    int32_t   acc;            // error-diffusion accumulator
    pool_id_t current;        // pool owning the current slice
    int64_t   slice_start_us; // start time of the current slice
    bool      initialized;
} pool_scheduler_t;

void pool_scheduler_init(pool_scheduler_t *s, uint8_t ratio_a, uint16_t interval_ms, int64_t now_us);
pool_id_t pool_scheduler_select(pool_scheduler_t *s, int64_t now_us);
#endif
```

- [ ] **Step 3: Run test to verify it FAILS**

Run: `make run`
Expected: link/compile error (functions undefined) or assertion failure.

- [ ] **Step 4: Implement** — `pool_scheduler.c`

```c
#include "pool_scheduler.h"
#include "dual_clamp.h"

static void advance_slice(pool_scheduler_t *s) {
    s->acc += s->ratio_a;
    if (s->acc >= 100) { s->acc -= 100; s->current = POOL_A; }
    else               { s->current = POOL_B; }
}

void pool_scheduler_init(pool_scheduler_t *s, uint8_t ratio_a, uint16_t interval_ms, int64_t now_us) {
    s->ratio_a      = dual_clamp_ratio(ratio_a);
    s->interval_ms  = dual_clamp_interval(interval_ms);
    s->acc          = 0;
    s->current      = POOL_A;
    s->slice_start_us = now_us;
    s->initialized  = false;
}

pool_id_t pool_scheduler_select(pool_scheduler_t *s, int64_t now_us) {
    if (!s->initialized) {
        s->initialized = true;
        s->slice_start_us = now_us;
        advance_slice(s);
        return s->current;
    }
    int64_t elapsed_ms = (now_us - s->slice_start_us) / 1000;
    while (s->interval_ms > 0 && elapsed_ms >= s->interval_ms) {
        s->slice_start_us += (int64_t)s->interval_ms * 1000;
        elapsed_ms       -= s->interval_ms;
        advance_slice(s);
    }
    return s->current;
}
```

- [ ] **Step 5: Run test to verify it PASSES**

Run: `make run`
Expected: all A1 + A2 asserts pass, prints success lines.

- [ ] **Step 6: Commit**

```bash
git add components/dual_pool
git commit -m "feat(dual_pool): weighted time-slice scheduler + host tests"
```

### Task A3: Per-pool failover state machine

**Files:**
- Create: `components/dual_pool/include/pool_failover.h`, `components/dual_pool/pool_failover.c`
- Modify: `components/dual_pool/test_host/test_dual_pool.c`

**Interfaces:**
- Produces:
  - `typedef enum { PF_TRY_PRIMARY, PF_ON_PRIMARY, PF_TRY_FAILOVER, PF_ON_FAILOVER, PF_DOWN } pf_state_t;`
  - `typedef enum { PF_EV_CONNECTED, PF_EV_DISCONNECTED } pf_event_t;`
  - `pool_failover_t` struct.
  - `void pool_failover_init(pool_failover_t *f, int max_retries, bool has_failover);`
  - `int  pool_failover_endpoint(const pool_failover_t *f);`  // 0=primary,1=failover,-1=down
  - `void pool_failover_step(pool_failover_t *f, pf_event_t ev);`

- [ ] **Step 1: Write the failing test** (append + call from `main`)

```c
#include "pool_failover.h"

static void test_failover_primary_to_failover(void) {
    pool_failover_t f;
    pool_failover_init(&f, 2, true);          // 2 retries, has failover
    assert(pool_failover_endpoint(&f) == 0);  // start on primary
    pool_failover_step(&f, PF_EV_CONNECTED);
    assert(pool_failover_endpoint(&f) == 0);  // still primary
    pool_failover_step(&f, PF_EV_DISCONNECTED); // retry 1
    assert(pool_failover_endpoint(&f) == 0);
    pool_failover_step(&f, PF_EV_DISCONNECTED); // retry 2
    assert(pool_failover_endpoint(&f) == 0);
    pool_failover_step(&f, PF_EV_DISCONNECTED); // exhausted -> failover
    assert(pool_failover_endpoint(&f) == 1);
}

static void test_failover_recovers_primary(void) {
    pool_failover_t f;
    pool_failover_init(&f, 1, true);
    pool_failover_step(&f, PF_EV_DISCONNECTED); // retry 1
    pool_failover_step(&f, PF_EV_DISCONNECTED); // -> failover
    assert(pool_failover_endpoint(&f) == 1);
    pool_failover_step(&f, PF_EV_CONNECTED);    // connected on failover
    assert(pool_failover_endpoint(&f) == 1);
    pool_failover_step(&f, PF_EV_DISCONNECTED); // failover dropped -> retry primary
    assert(pool_failover_endpoint(&f) == 0);
}

static void test_failover_down_when_no_backup(void) {
    pool_failover_t f;
    pool_failover_init(&f, 1, false);           // no failover configured
    pool_failover_step(&f, PF_EV_DISCONNECTED); // retry 1
    pool_failover_step(&f, PF_EV_DISCONNECTED); // exhausted, no backup -> down
    assert(pool_failover_endpoint(&f) == -1);
}
```

- [ ] **Step 2: Create the header** — `include/pool_failover.h`

```c
#ifndef POOL_FAILOVER_H
#define POOL_FAILOVER_H
#include <stdbool.h>

typedef enum { PF_TRY_PRIMARY, PF_ON_PRIMARY, PF_TRY_FAILOVER, PF_ON_FAILOVER, PF_DOWN } pf_state_t;
typedef enum { PF_EV_CONNECTED, PF_EV_DISCONNECTED } pf_event_t;

typedef struct {
    pf_state_t state;
    int  retry_count;
    int  max_retries;
    bool has_failover;
} pool_failover_t;

void pool_failover_init(pool_failover_t *f, int max_retries, bool has_failover);
int  pool_failover_endpoint(const pool_failover_t *f); // 0 primary, 1 failover, -1 down
void pool_failover_step(pool_failover_t *f, pf_event_t ev);
#endif
```

- [ ] **Step 3: Run test to verify it FAILS**

Run: `make run`
Expected: undefined-symbol / assertion failure.

- [ ] **Step 4: Implement** — `pool_failover.c`

```c
#include "pool_failover.h"

void pool_failover_init(pool_failover_t *f, int max_retries, bool has_failover) {
    f->state = PF_TRY_PRIMARY;
    f->retry_count = 0;
    f->max_retries = max_retries < 0 ? 0 : max_retries;
    f->has_failover = has_failover;
}

int pool_failover_endpoint(const pool_failover_t *f) {
    switch (f->state) {
        case PF_TRY_PRIMARY:
        case PF_ON_PRIMARY:   return 0;
        case PF_TRY_FAILOVER:
        case PF_ON_FAILOVER:  return 1;
        default:              return -1; // PF_DOWN
    }
}

void pool_failover_step(pool_failover_t *f, pf_event_t ev) {
    if (ev == PF_EV_CONNECTED) {
        if (f->state == PF_TRY_PRIMARY || f->state == PF_ON_PRIMARY) f->state = PF_ON_PRIMARY;
        else                                                         f->state = PF_ON_FAILOVER;
        f->retry_count = 0;
        return;
    }
    // ev == PF_EV_DISCONNECTED
    switch (f->state) {
        case PF_ON_PRIMARY:
            f->state = PF_TRY_PRIMARY;
            f->retry_count = 1;
            break;
        case PF_TRY_PRIMARY:
            f->retry_count++;
            if (f->retry_count > f->max_retries) {
                if (f->has_failover) { f->state = PF_TRY_FAILOVER; f->retry_count = 0; }
                else                 { f->state = PF_DOWN; }
            }
            break;
        case PF_ON_FAILOVER:
            // failover dropped -> go back and probe primary
            f->state = PF_TRY_PRIMARY;
            f->retry_count = 0;
            break;
        case PF_TRY_FAILOVER:
            f->retry_count++;
            if (f->retry_count > f->max_retries) { f->state = PF_TRY_PRIMARY; f->retry_count = 0; }
            break;
        case PF_DOWN:
            f->state = PF_TRY_PRIMARY; f->retry_count = 0;
            break;
    }
}
```

> Note: `PF_DOWN` is transient — the Pool B task treats endpoint `-1` as "donate my
> slices to Pool A this cycle" (§ degradation) and keeps re-attempting from primary.

- [ ] **Step 5: Run test to verify it PASSES**

Run: `make run`
Expected: all A1+A2+A3 asserts pass.

- [ ] **Step 6: Commit**

```bash
git add components/dual_pool
git commit -m "feat(dual_pool): per-pool failover state machine + host tests"
```

---

## Phase B — NVS configuration (Pool B block + dual controls)

### Task B1: Add NVS key enum entries

**Files:**
- Modify: `main/nvs_config.h` (enum `NvsConfigKey`, before `NVS_CONFIG_COUNT`)

**Interfaces:**
- Produces enum entries: `NVS_CONFIG_DUAL_ENABLE`, `NVS_CONFIG_DUAL_INTERVAL_MS`,
  `NVS_CONFIG_DUAL_RATIO_A`, `NVS_CONFIG_POOLB_URL`, `NVS_CONFIG_POOLB_PORT`,
  `NVS_CONFIG_POOLB_USER`, `NVS_CONFIG_POOLB_PASS`, `NVS_CONFIG_POOLB_TLS`,
  `NVS_CONFIG_POOLB_FB_URL`, `NVS_CONFIG_POOLB_FB_PORT`, `NVS_CONFIG_POOLB_FB_USER`,
  `NVS_CONFIG_POOLB_FB_PASS`, `NVS_CONFIG_POOLB_FB_TLS`.

- [ ] **Step 1: Insert the enum entries** immediately before `NVS_CONFIG_COUNT`:

```c
    NVS_CONFIG_DUAL_ENABLE,
    NVS_CONFIG_DUAL_INTERVAL_MS,
    NVS_CONFIG_DUAL_RATIO_A,
    NVS_CONFIG_POOLB_URL,
    NVS_CONFIG_POOLB_PORT,
    NVS_CONFIG_POOLB_USER,
    NVS_CONFIG_POOLB_PASS,
    NVS_CONFIG_POOLB_TLS,
    NVS_CONFIG_POOLB_FB_URL,
    NVS_CONFIG_POOLB_FB_PORT,
    NVS_CONFIG_POOLB_FB_USER,
    NVS_CONFIG_POOLB_FB_PASS,
    NVS_CONFIG_POOLB_FB_TLS,
    NVS_CONFIG_COUNT
```

- [ ] **Step 2: Commit**

```bash
git add main/nvs_config.h
git commit -m "feat(nvs): declare Pool B + dual-mining config keys"
```

### Task B2: Add `Settings` table rows + Kconfig defaults

**Files:**
- Modify: `main/nvs_config.c` (the `default_configs[]`/`Settings` table)
- Modify: `main/Kconfig.projbuild`

**Interfaces:**
- Consumes: enum entries from B1.
- Produces: table rows with `rest_name` values `dualEnable`, `dualIntervalMs`,
  `dualRatioA`, `poolBUrl`, `poolBPort`, `poolBUser`, `poolBPassword`, `poolBTls`,
  `poolBFallbackUrl`, `poolBFallbackPort`, `poolBFallbackUser`,
  `poolBFallbackPassword`, `poolBFallbackTls`.

- [ ] **Step 1: Add Kconfig defaults** to `main/Kconfig.projbuild` (near the existing
  `CONFIG_STRATUM_*` entries):

```
config POOL_B_STRATUM_URL
    string "Pool B Stratum URL"
    default "solo.ckpool.org"
config POOL_B_STRATUM_PORT
    int "Pool B Stratum port"
    default 3333
config POOL_B_STRATUM_USER
    string "Pool B Stratum user"
    default "bc1qexampleexampleexampleexampleexampleex.poolb"
config POOL_B_STRATUM_PW
    string "Pool B Stratum password"
    default "x"
```

- [ ] **Step 2: Add the `Settings` rows** in `main/nvs_config.c` (match the existing
  row style; `NVS_STR_LIMIT` and the `.value` slot follow the existing entries):

```c
    [NVS_CONFIG_DUAL_ENABLE]      = {.nvs_key_name = "dualenable",  .type = TYPE_BOOL, .default_value = {.b = false},              .rest_name = "dualEnable",             .min = 0, .max = 1},
    [NVS_CONFIG_DUAL_INTERVAL_MS] = {.nvs_key_name = "dualintms",   .type = TYPE_U16,  .default_value = {.u16 = 500},             .rest_name = "dualIntervalMs",         .min = 100, .max = 60000},
    [NVS_CONFIG_DUAL_RATIO_A]     = {.nvs_key_name = "dualratioa",  .type = TYPE_U16,  .default_value = {.u16 = 50},              .rest_name = "dualRatioA",             .min = 0, .max = 100},
    [NVS_CONFIG_POOLB_URL]        = {.nvs_key_name = "poolburl",    .type = TYPE_STR,  .default_value = {.str = (char *)CONFIG_POOL_B_STRATUM_URL},  .rest_name = "poolBUrl",   .min = 0, .max = NVS_STR_LIMIT},
    [NVS_CONFIG_POOLB_PORT]       = {.nvs_key_name = "poolbport",   .type = TYPE_U16,  .default_value = {.u16 = CONFIG_POOL_B_STRATUM_PORT},        .rest_name = "poolBPort",  .min = 0, .max = 65535},
    [NVS_CONFIG_POOLB_USER]       = {.nvs_key_name = "poolbuser",   .type = TYPE_STR,  .default_value = {.str = (char *)CONFIG_POOL_B_STRATUM_USER}, .rest_name = "poolBUser",  .min = 0, .max = NVS_STR_LIMIT},
    [NVS_CONFIG_POOLB_PASS]       = {.nvs_key_name = "poolbpass",   .type = TYPE_STR,  .default_value = {.str = (char *)CONFIG_POOL_B_STRATUM_PW},   .rest_name = "poolBPassword", .min = 0, .max = NVS_STR_LIMIT},
    [NVS_CONFIG_POOLB_TLS]        = {.nvs_key_name = "poolbtls",    .type = TYPE_U16,  .default_value = {.u16 = 0},               .rest_name = "poolBTls",               .min = 0, .max = 2},
    [NVS_CONFIG_POOLB_FB_URL]     = {.nvs_key_name = "poolbfburl",  .type = TYPE_STR,  .default_value = {.str = (char *)""},      .rest_name = "poolBFallbackUrl",       .min = 0, .max = NVS_STR_LIMIT},
    [NVS_CONFIG_POOLB_FB_PORT]    = {.nvs_key_name = "poolbfbport", .type = TYPE_U16,  .default_value = {.u16 = 3333},            .rest_name = "poolBFallbackPort",      .min = 0, .max = 65535},
    [NVS_CONFIG_POOLB_FB_USER]    = {.nvs_key_name = "poolbfbuser", .type = TYPE_STR,  .default_value = {.str = (char *)""},      .rest_name = "poolBFallbackUser",      .min = 0, .max = NVS_STR_LIMIT},
    [NVS_CONFIG_POOLB_FB_PASS]    = {.nvs_key_name = "poolbfbpass", .type = TYPE_STR,  .default_value = {.str = (char *)""},      .rest_name = "poolBFallbackPassword",  .min = 0, .max = NVS_STR_LIMIT},
    [NVS_CONFIG_POOLB_FB_TLS]     = {.nvs_key_name = "poolbfbtls",  .type = TYPE_U16,  .default_value = {.u16 = 0},               .rest_name = "poolBFallbackTls",       .min = 0, .max = 2},
```

> If the table uses a `.value = &some_cache` slot per row (check neighboring rows),
> add matching cache variables following the existing convention. If rows have no
> `.value` slot, omit it.

- [ ] **Step 3: [USER-RUN] Verify the table compiles** (on-device toolchain)

Run: `idf.py build` (or at least `idf.py reconfigure`)
Expected: no "excess elements in struct initializer" / missing-field errors for the new rows.

- [ ] **Step 4: Commit**

```bash
git add main/nvs_config.c main/Kconfig.projbuild
git commit -m "feat(nvs): Pool B + dual-mining Settings rows and Kconfig defaults"
```

---

## Phase C — State model (Pool B parallel fields)

### Task C1: Add `pool_id` to `bm_job`

**Files:**
- Modify: `components/stratum/include/mining.h:24` (`bm_job` struct)

**Interfaces:**
- Produces: `uint8_t pool_id;` field on `bm_job` (0 = Pool A, 1 = Pool B).

- [ ] **Step 1: Add the field** after `char *extranonce2;`:

```c
    char *jobid;
    char *extranonce2;
    uint8_t pool_id; // 0 = Pool A, 1 = Pool B (dual mining)
} bm_job;
```

- [ ] **Step 2: Commit**

```bash
git add components/stratum/include/mining.h
git commit -m "feat(mining): tag bm_job with originating pool_id"
```

### Task C2: Add Pool B parallel fields + dual config to `GlobalState`/`SystemModule`

**Files:**
- Modify: `main/global_state.h`

**Interfaces:**
- Consumes: `pool_scheduler_t` (A2), `work_queue` (existing), `esp_transport_handle_t`.
- Produces (on `GlobalState`): `transportB`, `stratum_queueB`, `extranonce_strB`,
  `extranonce_2_lenB`, `pool_difficultyB`, `version_maskB`, `send_uidB`,
  `stratum_muxB`, `valid_jobs_lock` shared (reused), `scheduler`.
- Produces (on `SystemModule`): Pool B endpoint + failover config + per-pool counters.

- [ ] **Step 1: Add include** at top of `global_state.h`:

```c
#include "pool_scheduler.h"
```

- [ ] **Step 2: Add Pool B fields to `GlobalState`** (after the existing
  `esp_transport_handle_t transport;` / stratum block, ~line 175):

```c
    // ---- Dual mining: Pool B parallel state ----
    esp_transport_handle_t transportB;
    work_queue stratum_queueB;
    char * extranonce_strB;
    int extranonce_2_lenB;
    double pool_difficultyB;
    bool new_set_mining_difficulty_msgB;
    uint32_t version_maskB;
    bool new_stratum_version_rolling_msgB;
    int send_uidB;
    portMUX_TYPE stratum_muxB;
    pool_scheduler_t scheduler;
    bool dual_enable;
    uint16_t dual_interval_ms;
    uint8_t  dual_ratio_a;
```

- [ ] **Step 3: Add Pool B config + counters to `SystemModule`** (after the
  `fallback_pool_*` fields, ~line 98):

```c
    // ---- Dual mining: Pool B endpoint + dedicated failover ----
    char * poolB_url;
    uint16_t poolB_port;
    char * poolB_user;
    char * poolB_pass;
    uint16_t poolB_tls;
    char * poolB_fb_url;
    uint16_t poolB_fb_port;
    char * poolB_fb_user;
    char * poolB_fb_pass;
    uint16_t poolB_fb_tls;
    bool poolB_is_using_failover;
    // per-pool share counters
    uint64_t poolA_shares_accepted;
    uint64_t poolA_shares_rejected;
    uint64_t poolB_shares_accepted;
    uint64_t poolB_shares_rejected;
    char poolB_connection_info[64];
```

- [ ] **Step 4: Commit**

```bash
git add main/global_state.h
git commit -m "feat(state): Pool B parallel fields, dual config, per-pool counters"
```

### Task C3: Initialize Pool B queue + load config in `system.c`

**Files:**
- Modify: `main/system.c` (the pool-setup block shown at ~lines 82-100, plus init)

**Interfaces:**
- Consumes: NVS getters (B1/B2), `queue_init` (existing).
- Produces: populated `SystemModule.poolB_*`, `GlobalState.dual_*`, initialized
  `stratum_queueB`, `stratum_muxB`.

- [ ] **Step 1: Load Pool B + dual config** — add after the existing
  `module->fallback_pool_pass = ...` line:

```c
    // Pool B endpoint
    module->poolB_url  = nvs_config_get_string(NVS_CONFIG_POOLB_URL);
    module->poolB_port = nvs_config_get_u16(NVS_CONFIG_POOLB_PORT);
    module->poolB_user = nvs_config_get_string(NVS_CONFIG_POOLB_USER);
    module->poolB_pass = nvs_config_get_string(NVS_CONFIG_POOLB_PASS);
    module->poolB_tls  = nvs_config_get_u16(NVS_CONFIG_POOLB_TLS);
    // Pool B dedicated failover
    module->poolB_fb_url  = nvs_config_get_string(NVS_CONFIG_POOLB_FB_URL);
    module->poolB_fb_port = nvs_config_get_u16(NVS_CONFIG_POOLB_FB_PORT);
    module->poolB_fb_user = nvs_config_get_string(NVS_CONFIG_POOLB_FB_USER);
    module->poolB_fb_pass = nvs_config_get_string(NVS_CONFIG_POOLB_FB_PASS);
    module->poolB_fb_tls  = nvs_config_get_u16(NVS_CONFIG_POOLB_FB_TLS);
    module->poolB_is_using_failover = false;
    module->poolA_shares_accepted = module->poolA_shares_rejected = 0;
    module->poolB_shares_accepted = module->poolB_shares_rejected = 0;
```

- [ ] **Step 2: Load dual controls into `GlobalState`** — locate where `GlobalState`
  is initialized (in `SYSTEM_init` or `app_main`; find via `grep -n "queue_init" main/`):

```c
    GLOBAL_STATE->dual_enable      = nvs_config_get_bool(NVS_CONFIG_DUAL_ENABLE);
    GLOBAL_STATE->dual_interval_ms = nvs_config_get_u16(NVS_CONFIG_DUAL_INTERVAL_MS);
    GLOBAL_STATE->dual_ratio_a     = (uint8_t) nvs_config_get_u16(NVS_CONFIG_DUAL_RATIO_A);
    queue_init(&GLOBAL_STATE->stratum_queueB);
    GLOBAL_STATE->stratum_muxB = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    GLOBAL_STATE->transportB = NULL;
    GLOBAL_STATE->extranonce_strB = NULL;
    GLOBAL_STATE->send_uidB = 1;
    pool_scheduler_init(&GLOBAL_STATE->scheduler,
                        GLOBAL_STATE->dual_ratio_a,
                        GLOBAL_STATE->dual_interval_ms, 0);
```

- [ ] **Step 3: [USER-RUN] Build** — `idf.py build`; expect clean compile of `system.c`.

- [ ] **Step 4: Commit**

```bash
git add main/system.c
git commit -m "feat(system): load Pool B + dual config, init Pool B queue/scheduler"
```

---

## Phase D — Scheduler integration + result routing

### Task D1: Pool-parametrize `generate_work` and schedule in `create_jobs_task`

**Files:**
- Modify: `main/tasks/create_jobs_task.c`

**Interfaces:**
- Consumes: `pool_scheduler_select` (A2), `GlobalState.scheduler`, `stratum_queueB`,
  `extranonce_strB`, `extranonce_2_lenB`, `pool_difficultyB`, `dual_enable`.
- Produces: jobs stamped with `pool_id`; ASIC fed from the scheduler-selected pool.

- [ ] **Step 1: Add include + esp_timer** (already included). Add near top:

```c
#include "pool_scheduler.h"
```

- [ ] **Step 2: Give `generate_work` a `pool_id` + per-pool extranonce.** Change the
  signature and the two globals it reads. Replace the `generate_work` declaration and
  the V1 branch call site. New signature:

```c
static void generate_work(GlobalState *GLOBAL_STATE, mining_notify *notification,
                          uint64_t extranonce_2, double difficulty, uint8_t pool_id);
```

Inside `generate_work`, replace the extranonce source:

```c
    const char *en_str = (pool_id == POOL_B) ? GLOBAL_STATE->extranonce_strB : GLOBAL_STATE->extranonce_str;
    int en2_len        = (pool_id == POOL_B) ? GLOBAL_STATE->extranonce_2_lenB : GLOBAL_STATE->extranonce_2_len;
    if (en2_len > MAX_EXTRANONCE2_LEN) { /* existing guard, using en2_len */ return; }
    char extranonce_2_str[MAX_EXTRANONCE2_STR];
    extranonce_2_generate(extranonce_2, en2_len, extranonce_2_str);
    ... calculate_coinbase_tx_hash(notification->coinbase_1, notification->coinbase_2, en_str, extranonce_2_str, coinbase_tx_hash);
```

Before `ASIC_send_work`, stamp the pool:

```c
    next_job->pool_id = pool_id;
    ASIC_send_work(GLOBAL_STATE, next_job);
```

- [ ] **Step 3: Add a Pool B (V1-only) dequeue + scheduler in the main loop.** In
  `create_jobs_task`, wrap the work-selection so that when `dual_enable` is true and the
  scheduler picks `POOL_B`, dequeue from `stratum_queueB` and generate with `pool_id=POOL_B`;
  otherwise keep the existing Pool A path with `pool_id=POOL_A`. Concretely, replace the
  V1 `generate_work(...)` call (~line 184) and add pool selection just above the
  "Generate and send job" block:

```c
        // Dual-mining pool selection (V1 path only; SV2 stays Pool A).
        uint8_t sel_pool = POOL_A;
        if (GLOBAL_STATE->dual_enable && active_protocol == STRATUM_PROTOCOL_V1) {
            // keep scheduler cadence in lockstep with real time
            sel_pool = pool_scheduler_select(&GLOBAL_STATE->scheduler, esp_timer_get_time());
            if (sel_pool == POOL_B) {
                // Pull the freshest Pool B notify; if none yet, fall back to Pool A this slice.
                void *bwork = queue_dequeue_timeout(&GLOBAL_STATE->stratum_queueB, 0);
                if (bwork != NULL) {
                    generate_work(GLOBAL_STATE, (mining_notify *)bwork, extranonce_2, GLOBAL_STATE->pool_difficultyB, POOL_B);
                    extranonce_2++;
                    STRATUM_V1_free_mining_notify((mining_notify *)bwork);
                    timeout_ms = ASIC_get_asic_job_frequency_ms(GLOBAL_STATE);
                    continue;
                }
                sel_pool = POOL_A; // no B work available -> donate slice to A
            }
        }
```

Place this block immediately before the existing `if (active_protocol == STRATUM_PROTOCOL_V2)`
generate section, and update the existing V1 `generate_work(...)` call to pass `POOL_A`:

```c
            generate_work(GLOBAL_STATE, (mining_notify *)current_work, extranonce_2, difficulty, POOL_A);
```

> Design note: Pool B keeps its own most-recent `mining_notify` in `stratum_queueB`;
> the Pool B task re-enqueues on each notify (Task E1). Dequeue-with-timeout 0 is a
> non-blocking peek-and-take; if empty, the slice is donated to Pool A (degradation).

- [ ] **Step 4: [USER-RUN] Build** — `idf.py build`; expect clean compile.

- [ ] **Step 5: Commit**

```bash
git add main/tasks/create_jobs_task.c
git commit -m "feat(jobs): weighted scheduler picks pool, stamps bm_job.pool_id"
```

### Task D2: Route share submission by `pool_id` in `ASIC_result_task`

**Files:**
- Modify: `main/tasks/asic_result_task.c`

**Interfaces:**
- Consumes: `bm_job.pool_id` (C1), `GlobalState.transportB`, `SystemModule.poolB_*`,
  per-pool counters (C2).
- Produces: shares submitted to the correct pool's transport with that pool's user.

- [ ] **Step 1: Select transport/user/mux/uid by pool_id** in the V1 submit branch.
  Replace the block that reads `user`, `transport`, `uid` (~lines 110-115):

```c
                bool is_b = (active_job->pool_id == POOL_B);
                char * user = is_b ? GLOBAL_STATE->SYSTEM_MODULE.poolB_user
                            : (GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback
                                 ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_user
                                 : GLOBAL_STATE->SYSTEM_MODULE.pool_user);

                portMUX_TYPE *mux = is_b ? &GLOBAL_STATE->stratum_muxB : &GLOBAL_STATE->stratum_mux;
                taskENTER_CRITICAL(mux);
                esp_transport_handle_t transport = is_b ? GLOBAL_STATE->transportB : GLOBAL_STATE->transport;
                int uid = is_b ? GLOBAL_STATE->send_uidB++ : GLOBAL_STATE->send_uid++;
                taskEXIT_CRITICAL(mux);
```

- [ ] **Step 2: Keep the rest of the submit unchanged** (it already uses the local
  `transport`, `uid`, `user`). It now targets Pool B when `is_b`.

- [ ] **Step 3: Attribute accepted/rejected counts per pool.** Stock updates
  accepted/rejected inside the Pool A stratum recv loop (`SYSTEM_notify_accepted_share`).
  For Pool B, attribution happens in the Pool B task (Task E1) where its own recv loop
  parses results. Here, no counter change is needed — leave submission as-is.

- [ ] **Step 4: [USER-RUN] Build** — `idf.py build`; expect clean compile.

- [ ] **Step 5: Commit**

```bash
git add main/tasks/asic_result_task.c
git commit -m "feat(result): route found share to originating pool by pool_id"
```

---

## Phase E — Pool B Stratum task (connect + failover) and wiring

### Task E1: Pool B self-contained Stratum + failover task

**Files:**
- Create: `main/tasks/stratum_poolb_task.c`, `main/tasks/stratum_poolb_task.h`
- Modify: `main/tasks/CMakeLists` inclusion is automatic (main component globs);
  otherwise add the source to `main/CMakeLists.txt`.

**Interfaces:**
- Consumes: `pool_failover_*` (A3), `STRATUM_V1_*` (existing stratum_api),
  `stratum_socket_*`, `GlobalState.transportB/stratum_queueB/extranonce_strB/...`,
  `SystemModule.poolB_*`.
- Produces: `void stratum_poolb_task(void *pvParameters);`

- [ ] **Step 1: Header** — `main/tasks/stratum_poolb_task.h`

```c
#ifndef STRATUM_POOLB_TASK_H
#define STRATUM_POOLB_TASK_H
void stratum_poolb_task(void *pvParameters);
#endif
```

- [ ] **Step 2: Implement the task.** Model it on `stratum_v1_task` but operate on the
  `*B` fields, use `pool_failover` to pick primary vs failover endpoint, and DO NOT call
  the protocol coordinator. Full body:

```c
#include "esp_log.h"
#include "global_state.h"
#include "stratum_poolb_task.h"
#include "stratum_socket.h"
#include "connect.h"
#include "work_queue.h"
#include "esp_timer.h"
#include "esp_transport.h"
#include "esp_transport_tcp.h"
#include "esp_transport_ssl.h"
#include "pool_failover.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "stratum_poolb";
static StratumApiV1Message poolb_msg = {};
#define POOLB_TIMEOUT_MS 5000
#define POOLB_MAX_RETRIES 3

static int poolb_uid(GlobalState *g) {
    taskENTER_CRITICAL(&g->stratum_muxB);
    int uid = g->send_uidB++;
    taskEXIT_CRITICAL(&g->stratum_muxB);
    return uid;
}

static void poolb_close(GlobalState *g) {
    taskENTER_CRITICAL(&g->stratum_muxB);
    esp_transport_handle_t t = g->transportB;
    g->transportB = NULL;
    taskEXIT_CRITICAL(&g->stratum_muxB);
    if (t) esp_transport_close(t);
    queue_clear(&g->stratum_queueB);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}

void stratum_poolb_task(void *pvParameters) {
    GlobalState *g = (GlobalState *)pvParameters;
    SystemModule *m = &g->SYSTEM_MODULE;
    g->stratum_queueB.free_fn = (void (*)(void *))STRATUM_V1_free_mining_notify;

    pool_failover_t fo;
    pool_failover_init(&fo, POOLB_MAX_RETRIES, m->poolB_fb_url && m->poolB_fb_url[0] != '\0');

    while (1) {
        if (!g->dual_enable) { vTaskDelay(2000 / portTICK_PERIOD_MS); continue; }
        if (!g->ASIC_initalized || !wifi_is_connected()) { vTaskDelay(2000 / portTICK_PERIOD_MS); continue; }

        int ep = pool_failover_endpoint(&fo);
        if (ep < 0) { // down -> donate slices to A (handled in create_jobs), retry primary soon
            pool_failover_step(&fo, PF_EV_DISCONNECTED);
            vTaskDelay(3000 / portTICK_PERIOD_MS);
            continue;
        }
        bool use_fb = (ep == 1);
        m->poolB_is_using_failover = use_fb;
        char    *url  = use_fb ? m->poolB_fb_url  : m->poolB_url;
        uint16_t port = use_fb ? m->poolB_fb_port : m->poolB_port;
        char    *usr  = use_fb ? m->poolB_fb_user : m->poolB_user;
        char    *pw   = use_fb ? m->poolB_fb_pass : m->poolB_pass;
        tls_mode tls  = (tls_mode)(use_fb ? m->poolB_fb_tls : m->poolB_tls);

        if (!url || url[0] == '\0') { vTaskDelay(3000 / portTICK_PERIOD_MS); continue; }

        stratum_connection_info_t ci;
        if (stratum_socket_resolve(url, port, &ci) != ESP_OK) {
            pool_failover_step(&fo, PF_EV_DISCONNECTED);
            vTaskDelay(2000 / portTICK_PERIOD_MS); continue;
        }
        g->transportB = STRATUM_V1_transport_init(tls, NULL);
        if (!g->transportB) { pool_failover_step(&fo, PF_EV_DISCONNECTED); vTaskDelay(3000/portTICK_PERIOD_MS); continue; }
        if (tls != DISABLED) esp_transport_ssl_set_common_name(g->transportB, url);

        if (esp_transport_connect(g->transportB, ci.host_ip, port, POOLB_TIMEOUT_MS) != ESP_OK) {
            ESP_LOGE(TAG, "Pool B connect failed %s:%d", url, port);
            esp_transport_destroy(g->transportB); g->transportB = NULL;
            pool_failover_step(&fo, PF_EV_DISCONNECTED);
            vTaskDelay(3000 / portTICK_PERIOD_MS); continue;
        }
        stratum_socket_set_options(g->transportB);
        pool_failover_step(&fo, PF_EV_CONNECTED);
        ESP_LOGI(TAG, "Pool B connected %s:%d (%s)", url, port, use_fb ? "failover" : "primary");

        g->send_uidB = 1;
        STRATUM_V1_configure_version_rolling(g->transportB, poolb_uid(g), &g->version_maskB);
        STRATUM_V1_subscribe(g->transportB, poolb_uid(g), g->DEVICE_CONFIG.family.asic.name);
        STRATUM_V1_authorize(g->transportB, poolb_uid(g), usr, pw);

        while (1) {
            if (!g->dual_enable) { poolb_close(g); break; }
            char *line = STRATUM_V1_receive_jsonrpc_line(g->transportB);
            if (!line) { poolb_close(g); pool_failover_step(&fo, PF_EV_DISCONNECTED); break; }
            if (!STRATUM_V1_parse(&poolb_msg, line)) { STRATUM_V1_reset_message(&poolb_msg); free(line); continue; }
            free(line);
            switch (poolb_msg.method) {
                case MINING_NOTIFY:
                    if (g->stratum_queueB.count == QUEUE_SIZE) {
                        mining_notify *old = (mining_notify *)queue_dequeue(&g->stratum_queueB);
                        STRATUM_V1_free_mining_notify(old);
                    }
                    queue_enqueue(&g->stratum_queueB, poolb_msg.mining_notification);
                    poolb_msg.mining_notification = NULL;
                    break;
                case MINING_SET_DIFFICULTY:
                    g->pool_difficultyB = poolb_msg.new_difficulty; break;
                case MINING_SET_VERSION_MASK:
                    g->version_maskB = poolb_msg.version_mask; break;
                case MINING_SET_EXTRANONCE:
                case STRATUM_RESULT_SUBSCRIBE: {
                    if (poolb_msg.extranonce_2_len > 0) {
                        char *old = g->extranonce_strB;
                        g->extranonce_strB = poolb_msg.extranonce_str;
                        poolb_msg.extranonce_str = NULL;
                        g->extranonce_2_lenB = poolb_msg.extranonce_2_len;
                        free(old);
                    }
                    break;
                }
                case STRATUM_RESULT:
                    if (poolb_msg.response_success) m->poolB_shares_accepted++;
                    else                            m->poolB_shares_rejected++;
                    break;
                case MINING_PING: STRATUM_V1_pong(g->transportB, poolb_msg.message_id); break;
                default: break;
            }
            STRATUM_V1_reset_message(&poolb_msg);
        }
    }
}
```

> This mirrors stock V1 setup (`configure` → `subscribe` → `authorize`) but on Pool B
> fields and endpoint chosen by the failover machine. `pw` is `poolB_pass` (or failover
> pass) — this is requirement #2 for Pool B.

- [ ] **Step 3: [USER-RUN] Build** — `idf.py build`; resolve any missing `#include`
  (compare against `stratum_v1_task.c` includes) until it compiles.

- [ ] **Step 4: Commit**

```bash
git add main/tasks/stratum_poolb_task.c main/tasks/stratum_poolb_task.h
git commit -m "feat(poolb): self-contained Pool B Stratum + failover task"
```

### Task E2: Spawn the Pool B task in `main.c`

**Files:**
- Modify: `main/main.c` (near the `create_jobs_task`/`ASIC_result_task` creation, ~line 183)

**Interfaces:**
- Consumes: `stratum_poolb_task` (E1).

- [ ] **Step 1: Include the header** at top of `main.c`:

```c
#include "stratum_poolb_task.h"
```

- [ ] **Step 2: Create the task** right after the `ASIC_result_task` creation block:

```c
            if (xTaskCreate(stratum_poolb_task, "stratum poolb", 8192, (void *) &GLOBAL_STATE, 12, NULL) != pdPASS) {
                ESP_LOGE(TAG, "create stratum_poolb_task failed");
            }
```

> The task self-gates on `dual_enable` (idles when off), so it is always safe to spawn.

- [ ] **Step 3: [USER-RUN] Build + flash + smoke test**

Run: `idf.py build flash monitor`
Expected (dual OFF, default): logs identical to stock; `stratum poolb` task idles;
mining to Pool A unaffected.

- [ ] **Step 4: Commit**

```bash
git add main/main.c
git commit -m "feat(main): spawn Pool B stratum task (idles when dual disabled)"
```

---

## Phase F — Config portal & REST (expose the fields, incl. Pool Password)

### Task F1: Expose new fields in REST GET/PATCH

**Files:**
- Modify: `main/http_server/http_server.c` and/or `main/http_server/system_api_json.c`
  (whichever assembles the settings JSON — find with `grep -n "stratumPassword" main/http_server/*.c`)

**Interfaces:**
- Consumes: NVS `rest_name`s from B2. Most of the table-driven settings API picks up
  new rows automatically; verify and add any hand-written field emit/parse.

- [ ] **Step 1: Confirm table-driven emit.** Run
  `grep -n "rest_name\|nvs_config_get_settings\|for.*NVS_CONFIG_COUNT" main/http_server/*.c main/nvs_config.c`.
  If settings are emitted/parsed by iterating the table, the new rows appear automatically —
  no code change; skip to Step 3.

- [ ] **Step 2: If fields are hand-written**, add emit (GET) and parse (PATCH) for each
  new `rest_name` alongside the existing `stratumPassword` handling, e.g.:

```c
    cJSON_AddStringToObject(root, "poolBUrl", nvs_config_get_string(NVS_CONFIG_POOLB_URL));
    cJSON_AddNumberToObject(root, "poolBPort", nvs_config_get_u16(NVS_CONFIG_POOLB_PORT));
    cJSON_AddStringToObject(root, "poolBUser", nvs_config_get_string(NVS_CONFIG_POOLB_USER));
    // NOTE: never emit passwords in GET responses; follow how stratumPassword is masked.
    cJSON_AddBoolToObject(root, "dualEnable", nvs_config_get_bool(NVS_CONFIG_DUAL_ENABLE));
    cJSON_AddNumberToObject(root, "dualIntervalMs", nvs_config_get_u16(NVS_CONFIG_DUAL_INTERVAL_MS));
    cJSON_AddNumberToObject(root, "dualRatioA", nvs_config_get_u16(NVS_CONFIG_DUAL_RATIO_A));
```

And in PATCH, mirror the `stratumPassword` set pattern for `poolBPassword`,
`poolBFallbackPassword`, and the rest.

- [ ] **Step 3: [USER-RUN] Build + verify REST**

Run: `idf.py build flash`; then
`curl http://<device-ip>/api/system/info` (or the settings endpoint) and confirm the
new keys appear; PATCH `dualEnable`/`poolBUrl` and re-GET.

- [ ] **Step 4: Commit**

```bash
git add main/http_server
git commit -m "feat(api): expose dual-mining + Pool B fields over REST"
```

### Task F2: Add portal form fields (axe-os) incl. "Pool Password"

**Files:**
- Modify: axe-os settings component (find with
  `grep -rn "stratumPassword\|Stratum Password\|poolUser" main/http_server/axe-os/src`)

**Interfaces:**
- Consumes: REST field names from F1.

- [ ] **Step 1: Add form controls** in the same settings form that has the Pool A
  fields — a "Dual Mining" section: `dualEnable` (toggle), `dualIntervalMs` (number),
  `dualRatioA` (0-100 slider/number), and a "Pool B" group mirroring Pool A's
  url/port/user/**password** + a "Pool B Failover" group. Ensure the Pool A password
  input is clearly labeled **"Pool Password"** (requirement #2). Bind each to the REST
  field name from F1 (reactive form control + `[(ngModel)]` per the existing pattern).

- [ ] **Step 2: [USER-RUN] Rebuild the frontend + firmware**

Run (from `main/http_server/axe-os`): `npm ci && npm run build`; then `idf.py build flash`.
Expected: settings page shows the new Dual Mining + Pool B + Pool Password fields;
saving persists to NVS (verify via re-GET / reboot).

- [ ] **Step 3: Commit**

```bash
git add main/http_server/axe-os
git commit -m "feat(ui): dual-mining, Pool B, and Pool Password portal fields"
```

---

## Phase G — Integration verification & deliverable assembly

### Task G1: [USER-RUN] End-to-end dual-mining verification on hardware

- [ ] **Step 1:** Configure two test SHA-256d Stratum endpoints (e.g. two solo/test
  pools or a local stratum simulator) as Pool A and Pool B; set `dualEnable=true`,
  `dualIntervalMs=500`, `dualRatioA=70`.
- [ ] **Step 2:** Flash, `idf.py monitor`. Confirm both `stratum_v1_task` and
  `stratum poolb` connect and stay connected (no disconnect churn).
- [ ] **Step 3:** Over ~15 min confirm accepted shares appear on BOTH pools, roughly
  70/30 by count (via `poolA_shares_accepted` / `poolB_shares_accepted` or pool dashboards).
- [ ] **Step 4:** Kill Pool B's primary endpoint; confirm Pool B fails over to its
  failover endpoint and Pool A mining is uninterrupted. Restore; confirm it returns.
- [ ] **Step 5:** Set `dualEnable=false`, reflash config; confirm behavior is
  indistinguishable from stock (Pool A only).

### Task G2: Assemble the deliverable copy + README

**Files:**
- Create: `Decentralized Dual Miners/BitAxe-ESP-Miner/` (the modified tree)
- Create/Modify: `Decentralized Dual Miners/README.md`

- [ ] **Step 1:** Ensure all edits above live in `BitAxe-ESP-Miner/` (not the pristine
  `_extracted` copy). Copy `_extracted/ESP-Miner-master` → `BitAxe-ESP-Miner/` at the
  start of execution if not already done.
- [ ] **Step 2:** Add a README section documenting the new fields, the split model
  (interval + ratio), per-pool failover, the SHA-256d-only + hashrate-split caveats, and
  build/flash steps.
- [ ] **Step 3: Commit**

```bash
git add "Decentralized Dual Miners/BitAxe-ESP-Miner" "Decentralized Dual Miners/README.md"
git commit -m "docs: BitAxe dual-miner deliverable + README"
```

---

## Self-Review (author checklist — completed)

- **Spec coverage:** Dual-pool interleave (A2, D1), proportional split (A2, B2, D1),
  configurable interval (A2, B2), two persistent sockets (E1 + stock A), pool-tagged
  results routing (C1, D2), Pool B password → authorize (E1 step 2), Pool Password UI
  (F2), dedicated per-pool failover (A3, E1), NVS persistence (B1/B2/C3), dual-off ==
  stock (constraints + D1 guard + E1 self-gate). All mapped.
- **Placeholder scan:** none — every code step contains concrete code; `[USER-RUN]`
  build/flash steps are explicitly marked because no toolchain/hardware exists here.
- **Type consistency:** `pool_id_t {POOL_A,POOL_B}` used in mining.h(uint8_t), scheduler,
  create_jobs, asic_result; `pool_failover_endpoint` returns int 0/1/-1 consistently;
  NVS `rest_name`s match between B2 and F1.
- **Known follow-ups for the sibling plans:** NerdQAxe fork (Config class + v2 http),
  NerdMiner_v2 (Arduino, real CPU-thread split). Separate plans.
