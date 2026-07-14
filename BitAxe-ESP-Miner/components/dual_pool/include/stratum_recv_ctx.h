#ifndef STRATUM_RECV_CTX_H
#define STRATUM_RECV_CTX_H

#include <stddef.h>
#include "esp_transport.h"

// Reentrant JSON-RPC line receive using a caller-owned buffer (bufp/sizep start as
// {NULL, 0}). Lets a second concurrent Stratum connection (dual-mining Pool B) receive
// lines without sharing the global accumulator in the upstream stratum_api.c.
//
// Deliberately lives in the dual_pool component (not stratum_api.c) so the dual-pool
// changes stay off the high-churn upstream stratum file — see MAINTENANCE.md.
//
// Returns a malloc'd line (caller frees) or NULL on error (buffer freed + reset to NULL).
char *STRATUM_V1_receive_jsonrpc_line_ctx(esp_transport_handle_t transport, char **bufp, size_t *sizep);

#endif // STRATUM_RECV_CTX_H
