#ifndef STRATUM_POOLB_TASK_H
#define STRATUM_POOLB_TASK_H

// Self-contained Pool B stratum task for dual mining. Maintains its own V1
// connection (primary + dedicated failover) independent of the protocol
// coordinator. Idles when dual mining is disabled.
void stratum_poolb_task(void *pvParameters);

#endif // STRATUM_POOLB_TASK_H
