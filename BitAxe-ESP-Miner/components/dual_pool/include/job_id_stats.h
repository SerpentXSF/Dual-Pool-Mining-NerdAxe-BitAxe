#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Pool A and Pool B. */
#define JOBSTAT_POOL_COUNT 2
#define JOBSTAT_POOL_A 0
#define JOBSTAT_POOL_B 1

/**
 * Diagnostic counters for stratum job-ID reuse. See job_id_stats.c for why this
 * exists: it measures whether upstream's global duplicate-jobId filter would be
 * safe to adopt in a fork that mines two pools at once. Records only; the
 * mining path is unaffected.
 */
void jobstat_init(void);

/* Call once per mining.notify, with the pool it arrived on. */
void jobstat_record(int pool, const char *job_id);

/* Same ID seen twice on the same pool - what upstream's filter targets. */
uint32_t jobstat_dups(int pool);

/* Total notifies recorded, so the dup counts have a denominator. */
uint32_t jobstat_seen(int pool);

/* Same ID live on BOTH pools - work a naive global filter would discard. */
uint32_t jobstat_cross_collisions(void);

/**
 * Notifies whose sample was skipped because the lock was contended. Should stay
 * 0; a non-zero value means the dup/cross counts are under-sampled and must not
 * be read as "no duplicates occurred".
 */
uint32_t jobstat_skipped(void);

/**
 * Forget a pool's recent IDs. Call on every (re)connect to that pool: many pools
 * restart their job-ID sequence at "1" per session, so IDs carried across a
 * reconnect would otherwise register as same-pool duplicates that upstream's
 * filter - whose state also resets around a reconnect - would never have seen.
 */
void jobstat_reset_pool(int pool);
