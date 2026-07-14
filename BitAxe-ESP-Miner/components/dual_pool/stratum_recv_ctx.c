#include "stratum_recv_ctx.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_transport.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Reentrant receive for dual mining (moved out of stratum_api.c). Operates on a
// caller-owned buffer instead of the global json_rpc_buffer, so a second concurrent
// Stratum connection (Pool B) never corrupts the primary connection's accumulator.

static const char *TAG = "stratum_recv_ctx";
#define RX_BUFFER_SIZE 1024
#define RX_TIMEOUT_MS  5000

static void realloc_ctx(char **bufp, size_t *sizep, size_t len)
{
    size_t old = strlen(*bufp);
    size_t neu = old + len + 1;
    if (neu < *sizep) {
        return;
    }
    neu = neu + (RX_BUFFER_SIZE - (neu % RX_BUFFER_SIZE));
    void *nb = realloc(*bufp, neu);
    if (nb == NULL) {
        ESP_LOGE(TAG, "Restarting System because of ERROR: realloc failed in ctx receive");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        esp_restart();
    }
    *bufp = nb;
    memset(*bufp + old, 0, neu - old);
    *sizep = neu;
}

char *STRATUM_V1_receive_jsonrpc_line_ctx(esp_transport_handle_t transport, char **bufp, size_t *sizep)
{
    if (*bufp == NULL) {
        *bufp = malloc(RX_BUFFER_SIZE);
        if (*bufp == NULL) {
            ESP_LOGE(TAG, "Failed to allocate ctx receive buffer");
            return NULL;
        }
        *sizep = RX_BUFFER_SIZE;
        memset(*bufp, 0, RX_BUFFER_SIZE);
    }

    char *line = NULL;
    char recv_buffer[RX_BUFFER_SIZE];
    int nbytes;

    while (!strstr(*bufp, "\n")) {
        memset(recv_buffer, 0, RX_BUFFER_SIZE);
        nbytes = esp_transport_read(transport, recv_buffer, RX_BUFFER_SIZE - 1, RX_TIMEOUT_MS);
        if (nbytes < 0) {
            ESP_LOGE(TAG, "ctx transport read failed (code: %d)", nbytes);
            free(*bufp);
            *bufp = NULL;
            *sizep = 0;
            return NULL;
        }
        if (nbytes > 0) {
            realloc_ctx(bufp, sizep, nbytes);
            strncat(*bufp, recv_buffer, nbytes);
        }
    }

    size_t buflen = strlen(*bufp);
    char *newline_pos = strchr(*bufp, '\n');
    if (newline_pos) {
        size_t line_len = newline_pos - *bufp;
        line = strndup(*bufp, line_len);
        size_t remaining_len = buflen - line_len - 1;
        if (remaining_len > 0) {
            memmove(*bufp, newline_pos + 1, remaining_len);
            (*bufp)[remaining_len] = '\0';
        } else {
            (*bufp)[0] = '\0';
        }
    }
    return line;
}
