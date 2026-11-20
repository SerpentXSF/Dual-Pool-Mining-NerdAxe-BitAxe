#include "crt_bundle.h"

#include <stdbool.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

static const char *TAG = "crt_bundle";

// Exported by esp_crt_bundle.c but absent from its public header.
int esp_crt_verify_callback(void *buf, mbedtls_x509_crt *crt, int depth, uint32_t *flags);

extern const uint8_t bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t bundle_end[] asm("_binary_x509_crt_bundle_end");

// Per-root header is the same shape in both bundle layouts: a name length, a
// key length, then the subject DER and the SubjectPublicKeyInfo DER. Only the
// width/endianness of the lengths and the way roots are located differ.
#define CRT_HEADER_LEN 4

static uint16_t rd16be(const uint8_t *p) { return (uint16_t) ((p[0] << 8) | p[1]); }
static uint16_t rd16le(const uint8_t *p) { return (uint16_t) (p[0] | (p[1] << 8)); }
static uint32_t rd32le(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

// Compare one bundle root's stored subject+key against a presented certificate.
static bool root_matches(const mbedtls_x509_crt *crt, const uint8_t *name, size_t name_len, const uint8_t *key,
                         size_t key_len)
{
    return crt->subject_raw.len == name_len && !memcmp(crt->subject_raw.p, name, name_len) &&
           crt->pk_raw.len == key_len && !memcmp(crt->pk_raw.p, key, key_len);
}

// ESP-IDF changed the embedded bundle layout. Up to 5.3 it is a big-endian u16
// root count followed by the roots back to back. From 5.4 it is a list of
// native-endian u32 offsets (the first offset doubles as the list's own size,
// so count == offset[0]/4) followed by the roots, with the lengths little
// endian. Parsing one layout as the other yields garbage, so this is gated on
// the IDF version rather than sniffed at runtime.
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
#define BUNDLE_LAYOUT_OFFSET_LIST 1
#else
#define BUNDLE_LAYOUT_OFFSET_LIST 0
#endif

// Number of roots the parser can actually see, or 0 if the bundle does not look
// like the layout we were compiled for.
static size_t bundle_root_count(void)
{
    const size_t size = (size_t) (bundle_end - bundle_start);

#if BUNDLE_LAYOUT_OFFSET_LIST
    if (size < sizeof(uint32_t)) {
        return 0;
    }
    const uint32_t list_bytes = rd32le(bundle_start);
    if (list_bytes < sizeof(uint32_t) || list_bytes % sizeof(uint32_t) || list_bytes > size) {
        return 0;
    }
    return list_bytes / sizeof(uint32_t);
#else
    if (size < 2) {
        return 0;
    }
    return rd16be(bundle_start);
#endif
}

// True when a root in the bundle carries the same subject and public key as crt.
static bool is_bundle_root(const mbedtls_x509_crt *crt)
{
    const size_t size = (size_t) (bundle_end - bundle_start);
    const size_t count = bundle_root_count();

#if BUNDLE_LAYOUT_OFFSET_LIST
    for (size_t i = 0; i < count; i++) {
        const uint32_t off = rd32le(bundle_start + i * sizeof(uint32_t));
        if (off > size || size - off < CRT_HEADER_LEN) {
            return false;
        }
        const uint8_t *cert = bundle_start + off;
        const size_t name_len = rd16le(cert);
        const size_t key_len = rd16le(cert + 2);
        const uint8_t *name = cert + CRT_HEADER_LEN;
        if ((size_t) (bundle_end - name) < name_len || (size_t) (bundle_end - name - name_len) < key_len) {
            return false;
        }
        if (root_matches(crt, name, name_len, name + name_len, key_len)) {
            return true;
        }
    }
#else
    const uint8_t *p = bundle_start + 2;
    for (size_t i = 0; i < count; i++) {
        if ((size_t) (bundle_end - p) < CRT_HEADER_LEN) {
            return false;
        }
        const size_t name_len = rd16be(p);
        const size_t key_len = rd16be(p + 2);
        const uint8_t *name = p + CRT_HEADER_LEN;
        if ((size_t) (bundle_end - name) < name_len || (size_t) (bundle_end - name - name_len) < key_len) {
            return false;
        }
        if (root_matches(crt, name, name_len, name + name_len, key_len)) {
            return true;
        }
        p = name + name_len + key_len;
    }
#endif
    (void) size;
    return false;
}

static int verify_callback(void *buf, mbedtls_x509_crt *crt, int depth, uint32_t *flags)
{
    // Accept a certificate that carries the subject and public key of a root in
    // the bundle.
    //
    // Trust here rests on the KEY, never on this certificate's own signature.
    // mbedTLS has already checked every lower certificate against the key of the
    // certificate above it, so when this one's subject and SubjectPublicKeyInfo
    // match a bundle root, the path below it was verified under that root's
    // genuine key. A certificate forged to carry a root's name and key buys an
    // attacker nothing, at any depth: completing the handshake still needs the
    // matching private key.
    //
    // Do NOT assume NOT_TRUSTED only ever lands on the topmost certificate.
    // mbedtls_x509_crt_verify's chain walk returns a non-trusted parent even
    // when the child's signature under it is bad, so an intermediate can reach
    // this callback flagged. That is still safe by the argument above: if the
    // child's signature did not verify then the child itself carries
    // NOT_TRUSTED, does not match a bundle root, and is left to ESP-IDF's
    // callback, which checks it against a bundle key and rejects it.
    //
    // BAD_MD is masked for the same reason ESP-IDF masks it on a root it already
    // trusts: it describes the hash over this certificate's own signature, which
    // is not relied on. Every other flag -- BAD_KEY, BAD_PK, CN_MISMATCH,
    // EXPIRED, the key-usage flags -- makes the comparison fail and falls
    // through to ESP-IDF's callback with the flag still set.
    //
    // This neither adds nor removes an expiry check: MBEDTLS_HAVE_TIME_DATE
    // defaults to n and we do not set it, so mbedTLS does not check validity
    // dates on any path here.
    if ((*flags & ~MBEDTLS_X509_BADCERT_BAD_MD) == MBEDTLS_X509_BADCERT_NOT_TRUSTED && is_bundle_root(crt)) {
        ESP_LOGI(TAG, "cross-signed trusted root accepted at depth %d", depth);
        *flags = 0;
        return 0;
    }
    return esp_crt_verify_callback(buf, crt, depth, flags);
}

esp_err_t crt_bundle_attach(void *conf)
{
    const esp_err_t ret = esp_crt_bundle_attach(conf);
    if (ret != ESP_OK) {
        return ret;
    }

    // A layout mismatch would silently disable the cross-signed path and leave
    // TLS to Google Trust Services hosts failing for no visible reason, so say
    // so loudly instead.
    const size_t count = bundle_root_count();
    if (count == 0) {
        ESP_LOGE(TAG, "embedded CA bundle did not parse; cross-signed roots will be rejected");
    } else {
        ESP_LOGD(TAG, "%u roots in embedded CA bundle", (unsigned) count);
    }

    if (conf) {
        mbedtls_ssl_conf_verify((mbedtls_ssl_config *) conf, verify_callback, NULL);
    }
    return ret;
}
