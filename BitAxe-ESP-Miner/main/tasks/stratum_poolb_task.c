#include "esp_log.h"
#include "esp_system.h"
#include "global_state.h"
#include "stratum_poolb_task.h"
#include "stratum_api.h"
#include "stratum_socket.h"
#include "connect.h"
#include "work_queue.h"
#include "esp_timer.h"
#include "esp_transport.h"
#include "esp_transport_tcp.h"
#include "esp_transport_ssl.h"
#include "pool_failover.h"
#include "pool_scheduler.h"
#include "stratum_recv_ctx.h"
#include "freertos/task.h"
#include <pthread.h>
#include <string.h>

// Pool B mirrors the Pool A V1 setup (configure -> subscribe -> authorize) but on
// the parallel *B fields, chooses primary vs dedicated-failover endpoint via the
// pool_failover state machine, and does NOT touch the protocol coordinator (which
// owns Pool A's lifecycle). It uses the reentrant STRATUM_V1_receive_jsonrpc_line_ctx
// with its own receive buffer so it never races the shared Pool A accumulator.

static const char *TAG = "stratum_poolb";

#define POOLB_TRANSPORT_TIMEOUT_MS 5000
#define POOLB_MAX_RETRIES 3

static int poolb_next_uid(GlobalState *g)
{
    taskENTER_CRITICAL(&g->stratum_muxB);
    int uid = g->send_uidB++;
    taskEXIT_CRITICAL(&g->stratum_muxB);
    return uid;
}

static void poolb_close(GlobalState *g)
{
    // Hold transportB_lock across NULL-out + close + destroy so a Pool B share submit
    // in asic_result_task (which takes the same lock) can't be mid-write on this handle
    // as we free it. A blocking mutex is required — the spinlock stratum_muxB can't wrap
    // esp_transport_close/destroy.
    pthread_mutex_lock(&g->transportB_lock);
    esp_transport_handle_t t = g->transportB;
    g->transportB = NULL;
    if (t != NULL) {
        esp_transport_close(t);
        esp_transport_destroy(t);
    }
    pthread_mutex_unlock(&g->transportB_lock);
    queue_clear(&g->stratum_queueB);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}

void stratum_poolb_task(void *pvParameters)
{
    GlobalState *g = (GlobalState *)pvParameters;
    SystemModule *m = &g->SYSTEM_MODULE;

    g->stratum_queueB.free_fn = (void (*)(void *))STRATUM_V1_free_mining_notify;

    StratumApiV1Message poolb_msg = {0};
    char *rxbuf = NULL;
    size_t rxsize = 0;

    pool_failover_t fo;
    pool_failover_init(&fo, POOLB_MAX_RETRIES,
                       (m->poolB_fb_url != NULL && m->poolB_fb_url[0] != '\0'));

    while (1) {
        if (!g->dual_enable) {
            vTaskDelay(2000 / portTICK_PERIOD_MS);
            continue;
        }
        if (!g->ASIC_initalized || !wifi_is_connected()) {
            vTaskDelay(2000 / portTICK_PERIOD_MS);
            continue;
        }

        int ep = pool_failover_endpoint(&fo);
        if (ep < 0) {
            // Both endpoints exhausted this cycle: Pool B donates its slices to
            // Pool A (handled in create_jobs_task) and we retry the primary soon.
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

        if (url == NULL || url[0] == '\0') {
            // Endpoint not configured; advance failover and wait.
            pool_failover_step(&fo, PF_EV_DISCONNECTED);
            vTaskDelay(3000 / portTICK_PERIOD_MS);
            continue;
        }

        stratum_connection_info_t ci;
        if (stratum_socket_resolve(url, port, &ci) != ESP_OK) {
            ESP_LOGE(TAG, "Pool B DNS resolve failed for %s", url);
            pool_failover_step(&fo, PF_EV_DISCONNECTED);
            vTaskDelay(2000 / portTICK_PERIOD_MS);
            continue;
        }

        // Build + connect on a LOCAL handle so a failed transport is never published to
        // g->transportB (the submit path would otherwise see a half-open handle we then
        // free). Only publish — under transportB_lock — once the connection is up.
        esp_transport_handle_t t = STRATUM_V1_transport_init(tls, NULL);
        if (t == NULL) {
            ESP_LOGE(TAG, "Pool B transport init failed");
            pool_failover_step(&fo, PF_EV_DISCONNECTED);
            vTaskDelay(3000 / portTICK_PERIOD_MS);
            continue;
        }
        if (tls != DISABLED) {
            esp_transport_ssl_set_common_name(t, url);
        }

        if (esp_transport_connect(t, ci.host_ip, port, POOLB_TRANSPORT_TIMEOUT_MS) != ESP_OK) {
            ESP_LOGE(TAG, "Pool B connect failed %s:%d", url, port);
            esp_transport_close(t);
            esp_transport_destroy(t);
            pool_failover_step(&fo, PF_EV_DISCONNECTED);
            vTaskDelay(3000 / portTICK_PERIOD_MS);
            continue;
        }
        stratum_socket_set_options(t);
        pthread_mutex_lock(&g->transportB_lock);
        g->transportB = t;
        pthread_mutex_unlock(&g->transportB_lock);
        pool_failover_step(&fo, PF_EV_CONNECTED);
        snprintf(m->poolB_connection_info, sizeof(m->poolB_connection_info),
                 "%s%s", (ci.addr_family == AF_INET6) ? "IPv6" : "IPv4",
                 use_fb ? " (failover)" : "");
        ESP_LOGI(TAG, "Pool B connected %s:%d (%s)", url, port, use_fb ? "failover" : "primary");

        // Fresh receive buffer for this connection.
        if (rxbuf != NULL) { free(rxbuf); rxbuf = NULL; rxsize = 0; }

        g->send_uidB = 1;
        STRATUM_V1_configure_version_rolling(g->transportB, poolb_next_uid(g), &g->version_maskB);
        STRATUM_V1_subscribe(g->transportB, poolb_next_uid(g), g->DEVICE_CONFIG.family.asic.name);
        int poolb_authorize_uid = poolb_next_uid(g);
        STRATUM_V1_authorize(g->transportB, poolb_authorize_uid, usr, pw);

        while (1) {
            if (!g->dual_enable) {
                poolb_close(g);
                if (rxbuf != NULL) { free(rxbuf); rxbuf = NULL; rxsize = 0; }
                break;
            }

            char *line = STRATUM_V1_receive_jsonrpc_line_ctx(g->transportB, &rxbuf, &rxsize);
            if (line == NULL) {
                ESP_LOGW(TAG, "Pool B connection lost, reconnecting");
                poolb_close(g);
                pool_failover_step(&fo, PF_EV_DISCONNECTED);
                break;
            }

            if (!STRATUM_V1_parse(&poolb_msg, line)) {
                STRATUM_V1_reset_message(&poolb_msg);
                free(line);
                continue;
            }
            free(line);

            switch (poolb_msg.method) {
                case MINING_NOTIFY:
                    if (poolb_msg.mining_notification->clean_jobs) {
                        if (g->stratum_queueB.count > 0) {
                            queue_clear(&g->stratum_queueB);
                        }
                        // Mirror of the Pool-A-side fix in SYSTEM_clean_jobs_queue: on a
                        // Pool B clean, invalidate only Pool B's in-flight ASIC slots so
                        // their late nonces aren't submitted to Pool B as stale rejects.
                        // Pool A's slots are left untouched.
                        pthread_mutex_lock(&g->valid_jobs_lock);
                        bm_job **jobs = g->ASIC_TASK_MODULE.active_jobs;
                        for (int i = 0; i < 128; i += 4) {
                            if (jobs != NULL && jobs[i] != NULL && jobs[i]->pool_id == POOL_B) {
                                g->valid_jobs[i] = 0;
                            }
                        }
                        pthread_mutex_unlock(&g->valid_jobs_lock);
                    }
                    if (g->stratum_queueB.count == QUEUE_SIZE) {
                        mining_notify *old = (mining_notify *)queue_dequeue(&g->stratum_queueB);
                        STRATUM_V1_free_mining_notify(old);
                    }
                    queue_enqueue(&g->stratum_queueB, poolb_msg.mining_notification);
                    poolb_msg.mining_notification = NULL; // ownership transferred to queue
                    break;

                case MINING_SET_DIFFICULTY:
                    ESP_LOGI(TAG, "Pool B set difficulty: %.2f", poolb_msg.new_difficulty);
                    g->pool_difficultyB = poolb_msg.new_difficulty;
                    break;

                case MINING_SET_VERSION_MASK:
                case STRATUM_RESULT_CONFIGURE:
                    g->version_maskB = poolb_msg.version_mask;
                    break;

                case MINING_SET_EXTRANONCE:
                case STRATUM_RESULT_SUBSCRIBE:
                    if (poolb_msg.extranonce_str != NULL && poolb_msg.extranonce_2_len > 0) {
                        // Swap the pointer under extranonceB_lock so create_jobs' generate_work
                        // (which copies the string under the same lock) never reads it mid-free.
                        pthread_mutex_lock(&g->extranonceB_lock);
                        char *old = g->extranonce_strB;
                        g->extranonce_strB = poolb_msg.extranonce_str;
                        g->extranonce_2_lenB = poolb_msg.extranonce_2_len;
                        pthread_mutex_unlock(&g->extranonceB_lock);
                        poolb_msg.extranonce_str = NULL; // ownership transferred
                        free(old); // safe: unreferenced once the swap is published under the lock
                        ESP_LOGI(TAG, "Pool B extranonce set, en2_len=%d", g->extranonce_2_lenB);
                    }
                    break;

                case STRATUM_RESULT:
                    // The authorize reply is a boolean result too; don't miscount it as an
                    // accepted share. (configure/subscribe come back as their own result
                    // methods above, so only authorize and real share acks reach here.)
                    if (poolb_msg.message_id == poolb_authorize_uid) {
                        if (!poolb_msg.response_success) {
                            ESP_LOGW(TAG, "Pool B authorize failed: %s",
                                     poolb_msg.error_str ? poolb_msg.error_str : "unknown");
                        }
                        break;
                    }
                    if (poolb_msg.response_success) {
                        m->poolB_shares_accepted++;
                    } else {
                        m->poolB_shares_rejected++;
                        ESP_LOGW(TAG, "Pool B share rejected: %s",
                                 poolb_msg.error_str ? poolb_msg.error_str : "unknown");
                    }
                    break;

                case MINING_PING:
                    STRATUM_V1_pong(g->transportB, poolb_msg.message_id);
                    break;

                case CLIENT_RECONNECT:
                    ESP_LOGW(TAG, "Pool B requested reconnect");
                    STRATUM_V1_reset_message(&poolb_msg);
                    poolb_close(g);
                    goto reconnect;

                default:
                    break;
            }
            STRATUM_V1_reset_message(&poolb_msg);
        }
    reconnect:
        continue;
    }
}
