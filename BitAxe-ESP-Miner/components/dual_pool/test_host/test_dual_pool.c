#include <assert.h>
#include <stdio.h>
#include "dual_clamp.h"
#include "pool_scheduler.h"
#include "pool_failover.h"

/* ---------- A1: clamps ---------- */
static void test_clamp_ratio(void) {
    assert(dual_clamp_ratio(-5)  == 0);
    assert(dual_clamp_ratio(0)   == 0);
    assert(dual_clamp_ratio(50)  == 50);
    assert(dual_clamp_ratio(100) == 100);
    assert(dual_clamp_ratio(150) == 100);
}

static void test_clamp_interval(void) {
    assert(dual_clamp_interval(0)     == 100);
    assert(dual_clamp_interval(50)    == 100);
    assert(dual_clamp_interval(500)   == 500);
    assert(dual_clamp_interval(60001) == 60000);
}

/* ---------- A2: scheduler ---------- */
static void test_scheduler_ratio_70_30(void) {
    pool_scheduler_t s;
    pool_scheduler_init(&s, 70, 500, 0);
    int a = 0, b = 0;
    for (int i = 0; i < 1000; i++) {
        int64_t now = (int64_t)i * 500 * 1000; /* us */
        if (pool_scheduler_select(&s, now) == POOL_A) a++; else b++;
    }
    assert(a >= 698 && a <= 702);
    assert(a + b == 1000);
}

static void test_scheduler_extremes(void) {
    pool_scheduler_t s;
    pool_scheduler_init(&s, 100, 500, 0);
    for (int i = 0; i < 50; i++)
        assert(pool_scheduler_select(&s, (int64_t)i * 500 * 1000) == POOL_A);
    pool_scheduler_init(&s, 0, 500, 0);
    for (int i = 0; i < 50; i++)
        assert(pool_scheduler_select(&s, (int64_t)i * 500 * 1000) == POOL_B);
}

static void test_scheduler_holds_within_slice(void) {
    pool_scheduler_t s;
    pool_scheduler_init(&s, 50, 500, 0);
    pool_id_t first = pool_scheduler_select(&s, 0);
    assert(pool_scheduler_select(&s, 100 * 1000) == first);
    assert(pool_scheduler_select(&s, 499 * 1000) == first);
}

/* ---------- A3: failover ---------- */
static void test_failover_primary_to_failover(void) {
    pool_failover_t f;
    pool_failover_init(&f, 2, true);
    assert(pool_failover_endpoint(&f) == 0);
    pool_failover_step(&f, PF_EV_CONNECTED);
    assert(pool_failover_endpoint(&f) == 0);
    pool_failover_step(&f, PF_EV_DISCONNECTED);
    assert(pool_failover_endpoint(&f) == 0);
    pool_failover_step(&f, PF_EV_DISCONNECTED);
    assert(pool_failover_endpoint(&f) == 0);
    pool_failover_step(&f, PF_EV_DISCONNECTED);
    assert(pool_failover_endpoint(&f) == 1);
}

static void test_failover_recovers_primary(void) {
    pool_failover_t f;
    pool_failover_init(&f, 1, true);
    pool_failover_step(&f, PF_EV_DISCONNECTED);
    pool_failover_step(&f, PF_EV_DISCONNECTED);
    assert(pool_failover_endpoint(&f) == 1);
    pool_failover_step(&f, PF_EV_CONNECTED);
    assert(pool_failover_endpoint(&f) == 1);
    pool_failover_step(&f, PF_EV_DISCONNECTED);
    assert(pool_failover_endpoint(&f) == 0);
}

static void test_failover_down_when_no_backup(void) {
    pool_failover_t f;
    pool_failover_init(&f, 1, false);
    pool_failover_step(&f, PF_EV_DISCONNECTED);
    pool_failover_step(&f, PF_EV_DISCONNECTED);
    assert(pool_failover_endpoint(&f) == -1);
}

int main(void) {
    test_clamp_ratio();
    test_clamp_interval();
    printf("A1 clamp tests passed\n");

    test_scheduler_ratio_70_30();
    test_scheduler_extremes();
    test_scheduler_holds_within_slice();
    printf("A2 scheduler tests passed\n");

    test_failover_primary_to_failover();
    test_failover_recovers_primary();
    test_failover_down_when_no_backup();
    printf("A3 failover tests passed\n");

    printf("ALL dual_pool host tests passed\n");
    return 0;
}
