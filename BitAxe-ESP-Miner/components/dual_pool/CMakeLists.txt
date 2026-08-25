idf_component_register(
    SRCS "dual_clamp.c" "pool_scheduler.c" "pool_failover.c" "stratum_recv_ctx.c" "job_id_stats.c"
    INCLUDE_DIRS "include"
    REQUIRES "tcp_transport"
)
