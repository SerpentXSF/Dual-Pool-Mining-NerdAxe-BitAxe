/**
 * Job-ID duplication statistics — diagnostic only, changes no behaviour.
 *
 * Exists to answer one question with data instead of assumption: would upstream
 * ESP-Miner's duplicate-jobId filter (PR #1731) be safe to adopt here?
 *
 * Upstream dedups incoming jobs against a FLAT, GLOBAL array of job-ID strings.
 * That is sound when one pool is active at a time, which is upstream's design.
 * It is not obviously sound here, because stratum job IDs are chosen by each
 * pool independently and are pool-local: two pools routinely both issue "1",
 * "2", "a". A global filter would treat Pool B's "1" as a duplicate of Pool A's
 * "1" and silently discard legitimate work — visible only as unexplained
 * hashrate loss, with no error anywhere.
 *
 * So this records two different things:
 *   dup[pool]  - the same job ID arriving twice on the SAME pool. This is what
 *                upstream's filter is actually for; if it is always zero here,
 *                the filter would buy us nothing.
 *   cross      - the same job ID live on BOTH pools at once. Every one of these
 *                is a share of work a naive global filter would have thrown
 *                away. If this is non-zero, adopting #1731 unmodified is unsafe
 *                and the dedup key must be (pool_index, job_id).
 *
 * Every known inaccuracy here biases toward OVER-counting, never under: IDs
 * longer than 23 chars are truncated (so two distinct long IDs could alias into
 * a false duplicate), and a pool that restarts its ID sequence would repeat
 * itself if jobstat_reset_pool() were not called on reconnect. That direction
 * matters: this data is used to justify NOT adopting a filter, so it must never
 * under-report. Note also that an SV2 Pool A session delivers work without
 * mining.notify, so a zero seen-count there means "not measured", not "no jobs".
 *
 * Cost is a bounded scan of two 16-entry arrays of short fixed-size strings, on
 * the stratum notify path. IDs are copied rather than referenced so nothing here
 * can outlive or race the caller's buffer. The lock is taken with a short
 * timeout and a failure simply skips the sample: losing a diagnostic count is
 * always preferable to stalling a mining task.
 */
#include <stdatomic.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "job_id_stats.h"

#define JOBSTAT_RING 16   /* recent IDs remembered per pool */
#define JOBSTAT_IDLEN 24  /* stratum job IDs are short; longer ones are truncated */

typedef struct {
    char ids[JOBSTAT_RING][JOBSTAT_IDLEN];
    int next;
} jobstat_ring_t;

static jobstat_ring_t s_ring[JOBSTAT_POOL_COUNT];

/* Atomic because the readers below are called from the HTTP task and the 500ms
   websocket broadcaster without taking the lock. Relaxed 32-bit loads/stores are
   single instructions on this core, so this costs nothing and removes a formal
   data race (and stays correct if link-time optimisation is ever enabled). */
static _Atomic uint32_t s_dup[JOBSTAT_POOL_COUNT];
static _Atomic uint32_t s_cross;
static _Atomic uint32_t s_seen[JOBSTAT_POOL_COUNT];
static _Atomic uint32_t s_skipped;
static SemaphoreHandle_t s_lock;
static StaticSemaphore_t s_lock_buf;

void jobstat_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);
    }
    memset(s_ring, 0, sizeof(s_ring));
    for (int i = 0; i < JOBSTAT_POOL_COUNT; i++) {
        atomic_store_explicit(&s_dup[i], 0, memory_order_relaxed);
        atomic_store_explicit(&s_seen[i], 0, memory_order_relaxed);
    }
    atomic_store_explicit(&s_cross, 0, memory_order_relaxed);
    atomic_store_explicit(&s_skipped, 0, memory_order_relaxed);
}

void jobstat_reset_pool(int pool)
{
    if (pool < 0 || pool >= JOBSTAT_POOL_COUNT || s_lock == NULL) {
        return;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(5)) != pdTRUE) {
        return;
    }
    memset(&s_ring[pool], 0, sizeof(s_ring[pool]));
    xSemaphoreGive(s_lock);
}

static bool ring_contains(const jobstat_ring_t *r, const char *id)
{
    for (int i = 0; i < JOBSTAT_RING; i++) {
        if (r->ids[i][0] != '\0' && strcmp(r->ids[i], id) == 0) {
            return true;
        }
    }
    return false;
}

void jobstat_record(int pool, const char *job_id)
{
    if (job_id == NULL || pool < 0 || pool >= JOBSTAT_POOL_COUNT || s_lock == NULL) {
        return;
    }

    char id[JOBSTAT_IDLEN];
    strncpy(id, job_id, sizeof(id) - 1);
    id[sizeof(id) - 1] = '\0';

    /* Short timeout: this sits on the notify path, and a missed diagnostic
       sample matters far less than delaying a job. */
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(5)) != pdTRUE) {
        /* Counted so a reader can tell "no duplicates" apart from "not measured".
           Without this the silence would be indistinguishable from a clean run,
           which is precisely the false reassurance this instrumentation exists
           to avoid. */
        atomic_fetch_add_explicit(&s_skipped, 1, memory_order_relaxed);
        return;
    }

    atomic_fetch_add_explicit(&s_seen[pool], 1, memory_order_relaxed);

    if (ring_contains(&s_ring[pool], id)) {
        atomic_fetch_add_explicit(&s_dup[pool], 1, memory_order_relaxed);
    }

    for (int p = 0; p < JOBSTAT_POOL_COUNT; p++) {
        if (p != pool && ring_contains(&s_ring[p], id)) {
            atomic_fetch_add_explicit(&s_cross, 1, memory_order_relaxed);
            break;
        }
    }

    jobstat_ring_t *r = &s_ring[pool];
    /* id is a fixed JOBSTAT_IDLEN buffer, already NUL-terminated above, so copy
       it whole. strncpy of IDLEN-1 trips -Wstringop-truncation here because the
       compiler cannot see that the terminator is already within range. */
    memcpy(r->ids[r->next], id, JOBSTAT_IDLEN);
    r->next = (r->next + 1) % JOBSTAT_RING;

    xSemaphoreGive(s_lock);
}

uint32_t jobstat_dups(int pool)
{
    if (pool < 0 || pool >= JOBSTAT_POOL_COUNT) {
        return 0;
    }
    return atomic_load_explicit(&s_dup[pool], memory_order_relaxed);
}

uint32_t jobstat_seen(int pool)
{
    if (pool < 0 || pool >= JOBSTAT_POOL_COUNT) {
        return 0;
    }
    return atomic_load_explicit(&s_seen[pool], memory_order_relaxed);
}

uint32_t jobstat_cross_collisions(void)
{
    return atomic_load_explicit(&s_cross, memory_order_relaxed);
}

uint32_t jobstat_skipped(void)
{
    return atomic_load_explicit(&s_skipped, memory_order_relaxed);
}
