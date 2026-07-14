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

// Initialize the scheduler. ratio_a/interval_ms are clamped. now_us is the
// current monotonic time in microseconds (0 is fine for tests).
void pool_scheduler_init(pool_scheduler_t *s, uint8_t ratio_a, uint16_t interval_ms, int64_t now_us);

// Return the pool that should own work right now, advancing slice boundaries
// as time passes. Within a single slice the returned pool is stable.
pool_id_t pool_scheduler_select(pool_scheduler_t *s, int64_t now_us);

#endif // POOL_SCHEDULER_H
