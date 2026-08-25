#include <stdint.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "websocket.h"
#include "websocket_log.h"
#include "websocket_api.h"
#include "http_server.h"
#include "log_buffer.h"

#define WS_LOG_SCRATCH_SIZE 2048

static const char * TAG = "websocket";

typedef struct {
    int fd;
    uint32_t type;
} ws_client_t;

static ws_client_t clients[MAX_WEBSOCKET_CLIENTS];
static int type_counts[WS_TYPE_MAX] = {0};
static SemaphoreHandle_t clients_mutex = NULL;
static httpd_handle_t server_handle = NULL;
static TaskHandle_t s_websocket_log_task_handle = NULL;

void websocket_set_log_task_handle(TaskHandle_t task_handle)
{
    s_websocket_log_task_handle = task_handle;
}

int websocket_get_active_client_count(WebSocketClientType type)
{
    if (type >= 0 && type < WS_TYPE_MAX) return type_counts[type];
    return 0;
}

void websocket_log_notify(void)
{
    if (s_websocket_log_task_handle != NULL && type_counts[WS_TYPE_LOGS] > 0) {
        xTaskNotifyGive(s_websocket_log_task_handle);
    }
}

esp_err_t websocket_add_client(int fd, WebSocketClientType type)
{
    if (xSemaphoreTake(clients_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex for adding client");
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_FAIL;
    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        if (clients[i].fd == -1) {
            clients[i].fd = fd;
            clients[i].type = type;
            if (type >= 0 && type < WS_TYPE_MAX) type_counts[type]++;

            ESP_LOGI(TAG, "Added WebSocket %s client, fd: %d, slot: %d", type == WS_TYPE_LOGS ? "log" : "api", fd, i);
            ret = ESP_OK;
            if (type == WS_TYPE_LOGS && s_websocket_log_task_handle) {
                xTaskNotifyGive(s_websocket_log_task_handle);
            }
            break;
        }
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Max WebSocket clients reached, cannot add fd: %d", fd);
    }

    xSemaphoreGive(clients_mutex);
    return ret;
}

void websocket_remove_client(int fd)
{
    if (xSemaphoreTake(clients_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex for removing client");
        return;
    }

    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        if (clients[i].fd == fd) {
            WebSocketClientType type = (WebSocketClientType)clients[i].type;
            clients[i].fd = -1;
            clients[i].type = 0;
            if (type >= 0 && type < WS_TYPE_MAX) type_counts[type]--;

            ESP_LOGI(TAG, "Removed WebSocket %s client, fd: %d, slot: %d", type == WS_TYPE_LOGS ? "log" : "api", fd, i);
            break;
        }
    }

    xSemaphoreGive(clients_mutex);
}

void websocket_send_to_client(int fd, httpd_ws_frame_t *pkt)
{
    if (server_handle == NULL || fd == -1) return;

    // httpd_ws_send_frame_async() does NOT validate the session: it resolves the
    // fd and writes straight to the socket. The httpd task can close a session
    // and lwip can hand the same fd to a brand-new (possibly plain-HTTP)
    // connection between our registry read and this send, which would inject WS
    // frame bytes into an unrelated response. httpd_ws_get_fd_info() reports
    // HTTPD_WS_CLIENT_WEBSOCKET only while ws_handshake_done && !ws_close, so it
    // rejects both a reused fd and a socket already closing.
    if (httpd_ws_get_fd_info(server_handle, fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
        return;
    }

    if (httpd_ws_send_frame_async(server_handle, fd, pkt) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send WebSocket frame to fd: %d", fd);
    }
}

void websocket_broadcast(WebSocketClientType type, httpd_ws_frame_t *pkt)
{
    if (server_handle == NULL) return;

    // Snapshot the matching fds under the lock, then send with the lock RELEASED.
    // Holding clients_mutex across the sends would let a slow/blocked send stall
    // websocket_remove_client past its 100ms mutex timeout, and that function
    // bails WITHOUT removing the client - leaking a registry slot and eventually
    // wedging the MAX_WEBSOCKET_CLIENTS cap for good.
    int fds[MAX_WEBSOCKET_CLIENTS];
    int n = 0;

    if (clients_mutex == NULL) return;
    if (xSemaphoreTake(clients_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire mutex for broadcast; skipping this tick");
        return;
    }
    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        if (clients[i].fd != -1 && clients[i].type == type) {
            fds[n++] = clients[i].fd;
        }
    }
    xSemaphoreGive(clients_mutex);

    for (int i = 0; i < n; i++) {
        websocket_send_to_client(fds[i], pkt);
    }
}

void websocket_close_fn(httpd_handle_t hd, int fd)
{
    websocket_remove_client(fd);
    close(fd);
}

void websocket_init(httpd_handle_t server)
{
    server_handle = server;
    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        clients[i].fd = -1;
        clients[i].type = 0;
    }

    if (clients_mutex == NULL) {
        clients_mutex = xSemaphoreCreateMutex();
    }
}

/**
 * Admission control, run BEFORE the handshake response is sent.
 *
 * Invoked via httpd_uri_t.ws_pre_handshake_cb (httpd_uri.c calls this ahead of
 * httpd_ws_respond_server_handshake). Because the socket is still plain HTTP at
 * this point, a rejected client can be told WHY with a real status code. Doing
 * these checks after the handshake - as this code used to - meant the only way
 * to refuse was to complete the 101 and then drop the socket, which the client
 * sees as a healthy connection that mysteriously dies, and which the browser
 * then retries every 5s forever.
 *
 * Requires CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT=y.
 */
esp_err_t websocket_on_pre_handshake(httpd_req_t *req)
{
    if (is_network_allowed(req) != ESP_OK) {
        ESP_LOGW(TAG, "Rejecting WebSocket client from a disallowed network");
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }

    // Only the httpd task mutates the registry, and this callback runs on it,
    // so this check-then-admit is serialised by construction - no lock needed
    // and no race with the matching add in websocket_on_handshake().
    int active_clients = 0;
    for (int i = 0; i < WS_TYPE_MAX; i++) active_clients += type_counts[i];
    if (active_clients >= MAX_WEBSOCKET_CLIENTS) {
        ESP_LOGE(TAG, "Max WebSocket clients reached, rejecting new connection");
        httpd_resp_send_custom_err(req, "429 Too Many Requests", "Max WebSocket clients reached");
        return ESP_FAIL;
    }

    return ESP_OK;
}

/**
 * Registers a newly-connected WebSocket client.
 *
 * Invoked via httpd_uri_t.ws_post_handshake_cb. As of ESP-IDF 5.5 the URI
 * handler is deliberately NOT called for a handshake - httpd_uri.c ends that
 * path with "If the request is websocket handshake, then do not call the
 * uri->handler". Registration used to live in websocket_handler behind a
 * handshake branch, which therefore never ran on 5.5: no client was ever added,
 * type_counts stayed 0, websocket_api_task sat in its "no clients" hibernate
 * path, and /api/ws/live accepted connections while never sending a byte.
 *
 * Admission is decided earlier, in websocket_on_pre_handshake(); by the time
 * this runs the 101 has been sent, so anything failing here can only close the
 * socket rather than report a status.
 * Requires CONFIG_HTTPD_WS_POST_HANDSHAKE_CB_SUPPORT=y.
 */
esp_err_t websocket_on_handshake(httpd_req_t *req)
{
    uint32_t type = (uint32_t)(uintptr_t)req->user_ctx;
    int fd = httpd_req_to_sockfd(req);

    // Send the initial full state BEFORE registering. Once the fd is in the
    // registry the 500ms websocket_api_task tick can broadcast to it from the
    // other core, and two concurrent httpd_ws_send_frame_async() calls on one
    // socket interleave their header/payload writes and corrupt the framing.
    // Sending first closes that window; the client may miss one <=500ms diff,
    // which is harmless because the UI merges partial updates. Admission was
    // already decided in websocket_on_pre_handshake() and only the httpd task
    // mutates the registry, so a slot is guaranteed to still be free here.
    if (type == WS_TYPE_API) {
        websocket_api_on_connect(fd);
    }

    if (websocket_add_client(fd, type) != ESP_OK) {
        ESP_LOGE(TAG, "Unexpected failure adding client, fd: %d", fd);
        return ESP_FAIL;
    }

    return ESP_OK;
}

/**
 * Handles WebSocket frames. On ESP-IDF >= 5.5 this is only ever reached for
 * frames; connection setup happens in websocket_on_handshake above.
 */
esp_err_t websocket_handler(httpd_req_t *req)
{
    // Handle WebSocket frame
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    // Get frame header to allow ESP-IDF to handle control frames (Ping/Pong/Close)
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        return ret;
    }

    // If there's a payload, drain it
    if (ws_pkt.len > 0) {
        uint8_t *buf = (uint8_t *)calloc(1, ws_pkt.len + 1);
        if (buf) {
            ws_pkt.payload = buf;
            ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
            free(buf);
            return ret;
        }
    }

    return ESP_OK;
}
