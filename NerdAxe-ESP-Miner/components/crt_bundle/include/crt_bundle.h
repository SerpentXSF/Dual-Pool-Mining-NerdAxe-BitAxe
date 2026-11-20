#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Drop-in replacement for esp_crt_bundle_attach(), using the same embedded
// bundle.
//
// ESP-IDF's bundle verify callback looks up only the *issuer* of the topmost
// certificate a server sends. A server that presents a cross-signed root fails
// when that issuer is not in the bundle, even when the root itself is. The
// pinned bundle contains "GTS Root R4" but not the "GlobalSign Root CA" that
// cross-signs it, so discord.com and other Google Trust Services hosts are
// rejected.
//
// This wrapper additionally accepts the topmost certificate when its subject
// and public key match a root already in the bundle. No root is added: it is
// the same trust anchor carrying a different issuer, and every verification
// flag other than the masked weak-hash one still goes to ESP-IDF's callback.
esp_err_t crt_bundle_attach(void *conf);

#ifdef __cplusplus
}
#endif
