#include <lwip/tcpip.h>

#include "system.h"
#include "work_queue.h"
#include "serial.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "nvs_config.h"
#include "utils.h"
#include "stratum_v2_task.h"
#include "sv2_protocol.h"
#include "hashrate_monitor_task.h"
#include "asic.h"
#include "freertos/task.h"
#include "scoreboard.h"
#include "self_test.h"
#include "pool_scheduler.h"

static const char *TAG = "asic_result";

void ASIC_result_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;

    while (1)
    {
        // Check if ASIC is initialized before trying to process work
        if (!GLOBAL_STATE->ASIC_initalized) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        task_result *asic_result = ASIC_process_work(GLOBAL_STATE);

        if (asic_result == NULL)
        {
            continue;
        }

        if (asic_result->register_type != REGISTER_INVALID) {
            hashrate_monitor_register_read(GLOBAL_STATE, asic_result->register_type, asic_result->asic_nr, asic_result->value, asic_result->timestamp_us);
            continue;
        }

        uint8_t job_id = asic_result->job_id;

        // Snapshot the job while holding the lock. The shared slot
        // (ASIC_TASK_MODULE.active_jobs[job_id]) can be freed and reused by
        // BM1370_send_work() while we run the (potentially multi-second, blocking)
        // share submit below; keeping a pointer into it is a use-after-free. The
        // bm_job body is inline and safe to copy by value — deep-copy the two
        // heap-owned strings so the snapshot stays valid after we unlock.
        pthread_mutex_lock(&GLOBAL_STATE->valid_jobs_lock);
        bool valid = (GLOBAL_STATE->valid_jobs[job_id] != 0) &&
                     (GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id] != NULL);
        if (!valid)
        {
            pthread_mutex_unlock(&GLOBAL_STATE->valid_jobs_lock);
            ESP_LOGW(TAG, "Invalid job nonce found, 0x%02X", job_id);
            continue;
        }
        bm_job active_job_snapshot = *GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id];
        active_job_snapshot.jobid = active_job_snapshot.jobid ? strdup(active_job_snapshot.jobid) : NULL;
        active_job_snapshot.extranonce2 = active_job_snapshot.extranonce2 ? strdup(active_job_snapshot.extranonce2) : NULL;
        pthread_mutex_unlock(&GLOBAL_STATE->valid_jobs_lock);
        bm_job *active_job = &active_job_snapshot;
        // check the nonce difficulty
        double nonce_diff = test_nonce_value(active_job, asic_result->nonce, asic_result->rolled_version);

        if (GLOBAL_STATE->SELF_TEST_MODULE.is_active) {
            self_test_record_nonce(GLOBAL_STATE, nonce_diff);
            free(active_job->jobid);
            free(active_job->extranonce2);
            continue;
        }

        uint32_t version_bits = asic_result->rolled_version ^ active_job->version;
        if (nonce_diff >= active_job->pool_diff)
        {
            // DUAL-POOL: route by the job's originating pool. Pool B is always V1;
            // Pool A follows the active stratum protocol (V1 or V2).
            bool is_b = (active_job->pool_id == POOL_B);
            if (!is_b && GLOBAL_STATE->stratum_protocol == STRATUM_PROTOCOL_V2) {
                // SV2: submit with binary protocol
                int ret;
                uint32_t sv2_job_id = (uint32_t)strtoul(active_job->jobid, NULL, 10);

                if (stratum_v2_is_extended_channel(GLOBAL_STATE)) {
                    sv2_conn_t *conn = GLOBAL_STATE->sv2_conn;
                    // SV2 spec: extranonce_size is the miner's rollable portion.
                    // The pool prepends its extranonce_prefix separately.
                    uint8_t en2_len = conn->extranonce_size;
                    uint8_t extranonce_2[32];
                    hex2bin(active_job->extranonce2, extranonce_2, en2_len);
                    ret = stratum_v2_submit_share_extended(GLOBAL_STATE, sv2_job_id,
                                                           asic_result->nonce,
                                                           active_job->ntime,
                                                           asic_result->rolled_version,
                                                           extranonce_2, en2_len);
                } else {
                    ret = stratum_v2_submit_share(GLOBAL_STATE, sv2_job_id,
                                                   asic_result->nonce,
                                                   active_job->ntime,
                                                   asic_result->rolled_version);
                }

                if (ret < 0) {
                    ESP_LOGW(TAG, "Failed to submit SV2 share (ret=%d, errno=%d: %s)",
                             ret, errno, strerror(errno));
                }
            } else {
                // V1: submit with JSON-RPC, routed to the originating pool.
                char * user;
                portMUX_TYPE *mux;
                esp_transport_handle_t transport;
                int uid;
                if (is_b) {
                    user = GLOBAL_STATE->SYSTEM_MODULE.poolB_is_using_failover
                             ? GLOBAL_STATE->SYSTEM_MODULE.poolB_fb_user
                             : GLOBAL_STATE->SYSTEM_MODULE.poolB_user;
                    mux = &GLOBAL_STATE->stratum_muxB;
                    taskENTER_CRITICAL(mux);
                    transport = GLOBAL_STATE->transportB;
                    uid = GLOBAL_STATE->send_uidB++;
                    taskEXIT_CRITICAL(mux);
                } else {
                    user = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback
                             ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_user
                             : GLOBAL_STATE->SYSTEM_MODULE.pool_user;
                    mux = &GLOBAL_STATE->stratum_mux;
                    taskENTER_CRITICAL(mux);
                    transport = GLOBAL_STATE->transport;
                    uid = GLOBAL_STATE->send_uid++;
                    taskEXIT_CRITICAL(mux);
                }

                if (transport == NULL) {
                    ESP_LOGW(TAG, "No stratum connection, dropping share (job 0x%02X, pool %c)", job_id, is_b ? 'B' : 'A');
                } else {
                    uint64_t sent_time_us = 0;
                    int ret = STRATUM_V1_submit_share(
                        transport,
                        uid,
                        user,
                        active_job->jobid,
                        active_job->extranonce2,
                        active_job->ntime,
                        asic_result->nonce,
                        version_bits,
                        &sent_time_us);

                    if (ret < 0) {
                        ESP_LOGW(TAG, "Unable to write share to socket (ret: %d, errno %d: %s)", ret, errno, strerror(errno));
                        // stratum_task recv loop will detect a broken connection on its next read and handle reconnection
                    }

                    float process_time = (sent_time_us - asic_result->timestamp_us) / 1000.0f;
                    GLOBAL_STATE->SYSTEM_MODULE.process_time = process_time;
                    ESP_LOGI(TAG, "Processing time: %0.1f ms", process_time);
                }
            }
        }
        // DUAL-POOL BEGIN: dropped-share recovery. Pool A and Pool B share the single
        // 128-slot ASIC job ring, so under rapid switching the slot this nonce belongs to
        // can be overwritten by the *other* pool's job before the nonce returns. It then
        // validates below difficulty against the wrong template and would be silently
        // dropped. Before losing it, re-test the nonce against every other live template
        // (cheap: a couple of SHA-256s each, first match wins) and, if one actually
        // satisfies its difficulty, submit the share to THAT pool instead. Only runs while
        // dual mining is active (V1), where the cross-pool slot collision can happen.
        else if (GLOBAL_STATE->dual_enable && GLOBAL_STATE->stratum_protocol == STRATUM_PROTOCOL_V1) {
            uint32_t vbits = asic_result->rolled_version ^ active_job->version;
            bool     rec_found   = false;
            uint8_t  rec_pool_id = POOL_A;
            char    *rec_jobid   = NULL;
            char    *rec_en2     = NULL;
            uint32_t rec_ntime   = 0;
            double   rec_diff    = 0;

            pthread_mutex_lock(&GLOBAL_STATE->valid_jobs_lock);
            for (int i = 0; i < 128; i++) {
                if (i == job_id || GLOBAL_STATE->valid_jobs[i] == 0) continue;
                bm_job *cand = GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[i];
                if (cand == NULL) continue;
                // The ASIC's rolled version bits (vbits) are template-independent; rebuild
                // the rolled version against this candidate's base version to test it.
                uint32_t cand_rv = cand->version | vbits;
                double d = test_nonce_value(cand, asic_result->nonce, cand_rv);
                if (d >= cand->pool_diff) {
                    rec_found   = true;
                    rec_pool_id = cand->pool_id;
                    rec_jobid   = cand->jobid ? strdup(cand->jobid) : NULL;
                    rec_en2     = cand->extranonce2 ? strdup(cand->extranonce2) : NULL;
                    rec_ntime   = cand->ntime;
                    rec_diff    = d;
                    break;
                }
            }
            pthread_mutex_unlock(&GLOBAL_STATE->valid_jobs_lock);

            if (rec_found && rec_jobid != NULL && rec_en2 != NULL) {
                bool is_b = (rec_pool_id == POOL_B);
                char *user;
                portMUX_TYPE *mux;
                esp_transport_handle_t transport;
                int uid;
                if (is_b) {
                    user = GLOBAL_STATE->SYSTEM_MODULE.poolB_is_using_failover
                             ? GLOBAL_STATE->SYSTEM_MODULE.poolB_fb_user
                             : GLOBAL_STATE->SYSTEM_MODULE.poolB_user;
                    mux = &GLOBAL_STATE->stratum_muxB;
                    taskENTER_CRITICAL(mux);
                    transport = GLOBAL_STATE->transportB;
                    uid = GLOBAL_STATE->send_uidB++;
                    taskEXIT_CRITICAL(mux);
                } else {
                    user = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback
                             ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_user
                             : GLOBAL_STATE->SYSTEM_MODULE.pool_user;
                    mux = &GLOBAL_STATE->stratum_mux;
                    taskENTER_CRITICAL(mux);
                    transport = GLOBAL_STATE->transport;
                    uid = GLOBAL_STATE->send_uid++;
                    taskEXIT_CRITICAL(mux);
                }
                if (transport != NULL) {
                    uint64_t sent_time_us = 0;
                    int ret = STRATUM_V1_submit_share(transport, uid, user, rec_jobid, rec_en2,
                                                      rec_ntime, asic_result->nonce, vbits, &sent_time_us);
                    if (ret < 0) {
                        ESP_LOGW(TAG, "Recovered-share submit failed (ret %d, errno %d: %s)", ret, errno, strerror(errno));
                    } else {
                        ESP_LOGI(TAG, "Recovered cross-slot share -> pool %c (diff %.1f, orig slot 0x%02X)",
                                 is_b ? 'B' : 'A', rec_diff, job_id);
                    }
                }
            }
            free(rec_jobid);
            free(rec_en2);
        }
        // DUAL-POOL END

        //log the ASIC response
        ESP_LOGI(TAG, "ID: %s, ASIC nr: %d, Core: %d/%d, ver: %08" PRIX32 " Nonce %08" PRIX32 " diff %.1f of %g.", active_job->jobid, asic_result->asic_nr, asic_result->core_id, asic_result->small_core_id, asic_result->rolled_version, asic_result->nonce, nonce_diff, active_job->pool_diff);

        SYSTEM_notify_found_nonce(GLOBAL_STATE, nonce_diff, active_job->target);

        scoreboard_add(&GLOBAL_STATE->SYSTEM_MODULE.scoreboard, nonce_diff, active_job->jobid, active_job->extranonce2, active_job->ntime, asic_result->nonce, version_bits);

        free(active_job->jobid);
        free(active_job->extranonce2);
    }
}
