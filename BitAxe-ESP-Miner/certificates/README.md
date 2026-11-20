# Embedded system CA bundle

This firmware pins curl's Mozilla-derived bundle from October 20, 2026 (118 roots)
instead of trusting whatever list the ESP-IDF in use happens to carry:

- Source: https://curl.se/ca/cacert-2026-10-20.pem
- Published checksum: https://curl.se/ca/cacert-2026-10-20.pem.sha256
- SHA-256: `796f1389bfea13c0ed78f663b765baaff54a508e5483786bd6ece73c5e3e3cbe`
- License: [Mozilla Public License 2.0](https://www.mozilla.org/MPL/2.0/), as
  documented by [curl](https://curl.se/docs/caextract.html).

The [eMudhra incident](https://emudhra.we-have-a-problem.com/) compromised
intermediate CA keys. This snapshot removes `emSign ECC Root CA - G3` and
excludes `AAA Certificate Services`, blocking the eMudhra and Sectigo
cross-signed paths. Removing a root also blocks legitimate servers whose only
trust path ran through it, so a pool must serve a chain to a root that remains.

`sdkconfig.defaults` disables ESP-IDF's full, common and deprecated lists and
enables only this custom bundle. ESP-IDF custom bundles are otherwise
**additive**, so re-enabling any default or deprecated list brings the affected
roots back: the IDF versions these trees build against (5.5.x for the BitAxe
tree, 5.3.x for the NerdAxe and Quad trees) all ship them. CMake refuses to configure a build whose
settings would bypass this, including a stale `sdkconfig` that overrides the
defaults.

If you have an existing checkout, back up `sdkconfig`, delete it and let
`idf.py reconfigure` regenerate it from `sdkconfig.defaults`, then reapply any
local settings you need. The guard runs during configuration, so fix this before
`idf.py menuconfig`. Use a clean build when changing trust stores.

The bundle is compiled into `esp-miner.bin`, so deployed miners need a firmware
OTA; updating the build host's trust store does nothing for them. Stratum's
**TLS (System certificate)** mode uses this bundle. **TLS (Custom certificate)**
still uses the certificate you configure and needs reviewing separately.

## Why there is a wrapper component

ESP-IDF's bundle verify callback resolves only the **issuer** of the topmost
certificate a server sends. That breaks any chain ending in a cross-signed root
whose issuer is not in the bundle — and this bundle is exactly such a case: it
contains `GTS Root R4` but not the `GlobalSign Root CA` that cross-signs it, so
`discord.com` and other Google Trust Services hosts would be rejected despite
their root being present. Upstream ESP-Miner does not hit this only because it
builds on ESP-IDF 6.x.

`components/crt_bundle` wraps the attach call and additionally accepts the
topmost certificate when its subject and public key match a root in the bundle:
the same trust anchor, reissued under a different issuer. Every call site uses
`crt_bundle_attach()` rather than `esp_crt_bundle_attach()`. No root is added.

The embedded bundle's binary layout differs between IDF 5.3 (a big-endian root
count, then the roots) and 5.4+ (a native-endian offset list), so the component
picks its parser from `ESP_IDF_VERSION`. Parsing the wrong layout is memory safe
and fails closed: no root matches, so cross-signed chains are simply rejected.

Be aware the "bundle did not parse" error only fires in one direction. Reading a
5.3-format bundle with the 5.4+ parser yields a count of 0 and logs. Reading a
5.4+ bundle with the 5.3 parser yields a large bogus count, matches nothing and
logs nothing, so that direction fails closed *silently*. No release ESP-IDF hits
this, since the gate matches the layout for every tag. Making the diagnostic
symmetric needs a full walk at attach time that has to land exactly on
`bundle_end`.

## Verification

```sh
python3 tools/test_ca_bundle.py
```

Checks the bundle checksum, the root count, the absence of the two affected
trust anchors, an accepted `ISRG Root X1` control, and rejection of all 12
published incident chains for an untrusted issuer. It disables certificate time
checks so expired fixtures cannot make the check pass by accident.

Note that `MBEDTLS_HAVE_TIME_DATE` is off by default and these trees do not
enable it, so the firmware does not check certificate validity dates on any
path. That is an upstream gap (`bitaxeorg/ESP-Miner#1563`), unchanged by this
bundle.

The test archive is an unmodified copy of
https://emudhra.we-have-a-problem.com/chains.zip captured on October 20, 2026,
SHA-256 `8a2edc81b6c51e3ad27d45ca1e441ec84c961e2dee0fc547f2a02c0f74315f95`. It
holds public certificates only, no private keys.

After building, with the ESP-IDF environment sourced, also confirm the generated
store matches a fresh conversion and is actually embedded:

```sh
python tools/test_ca_bundle.py --build-dir build
```

Host checks do not replace an on-device handshake. Before release, test the
primary and fallback pools, confirm a connection in system-certificate mode, and
review any configured custom CA separately.

To update the bundle later: download a dated snapshot and its published
checksum, verify both, update the filename in `sdkconfig.defaults` and
`CMakeLists.txt`, update the checksum and expected root count in the test, and
rerun the checks before distributing firmware. The build never downloads a
mutable CA bundle.
