#ifndef POOL_FAILOVER_H
#define POOL_FAILOVER_H
#include <stdbool.h>

typedef enum {
    PF_TRY_PRIMARY,
    PF_ON_PRIMARY,
    PF_TRY_FAILOVER,
    PF_ON_FAILOVER,
    PF_DOWN
} pf_state_t;

typedef enum {
    PF_EV_CONNECTED,
    PF_EV_DISCONNECTED
} pf_event_t;

typedef struct {
    pf_state_t state;
    int  retry_count;
    int  max_retries;
    bool has_failover;
} pool_failover_t;

void pool_failover_init(pool_failover_t *f, int max_retries, bool has_failover);

// Which endpoint to attempt/use now: 0 = primary, 1 = failover, -1 = down.
int pool_failover_endpoint(const pool_failover_t *f);

void pool_failover_step(pool_failover_t *f, pf_event_t ev);

#endif // POOL_FAILOVER_H
