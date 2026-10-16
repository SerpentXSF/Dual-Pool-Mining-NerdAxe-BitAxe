# Releasing

Checklist for cutting a release of this repository. It ships **two** firmware
families from one tag, which is where most of the sharp edges come from. Several
of the steps below have each already gone wrong once, so they are called out.

## 1. Bump the version of whichever tree changed

There are two version files and they move independently:

| file | firmware | current line |
|---|---|---|
| `BitAxe-ESP-Miner/version.txt` | BitAxe (AxeOS) | `SerpentX-DualPool-1.7` |
| `NerdAxe-ESP-Miner/version.txt` | NerdAxe (NerdOS) | `SerpentX-DualPool-1.5` |

Bump in the same commit as the code. Shipping changed code under a version
string that already shipped makes field reports impossible to interpret, and it
has already happened here: a NerdAxe was found running an unreleased build
wearing the `1.3` string, reporting a field the released `1.3` binary did not
even contain.

Note the release **tag** follows the BitAxe version. A NerdAxe therefore shows
"Update available" against a tag numbered higher than its own version even when
it is already running that release's NerdAxe binary. Known cosmetic wart of the
two-version scheme.

## 2. Build all three targets

```bash
# BitAxe (AxeOS) - needs ESP-IDF >= 5.5 and npm
docker run --rm --user root -v "$PWD/BitAxe-ESP-Miner":/home/builder/project \
  -w /home/builder/project esp-idf-builder-55:latest \
  bash -c '. /opt/esp/idf/export.sh; idf.py fullclean && idf.py build'

# NerdAxe and NerdAxeGamma - ESP-IDF 5.3.3, BOARD as an ENV var
for B in NERDAXE NERDAXEGAMMA; do
  docker run --rm --user root -e BOARD=$B -v "$PWD/NerdAxe-ESP-Miner":/home/builder/project \
    -w /home/builder/project esp-idf-builder:latest \
    bash -c '. /opt/esp/idf/export.sh; idf.py fullclean && idf.py build'
done
```

**`idf.py build` alone does NOT rebuild the web UI after a TypeScript change.**
It reuses the cached Angular output, so a UI fix can appear to build fine and
simply not be in `www.bin`. This cost a full debugging round once: the device
kept enforcing the old filename check after an apparently successful build.
Delete `main/http_server/axe-os/dist` (or `fullclean`) whenever any file under
`axe-os/src` changed, and confirm the lazy chunk hash moved — the chunk name
changes on every real rebuild, so it is a reliable fingerprint:

```bash
ls NerdAxe-ESP-Miner/main/http_server/axe-os/dist/axe-os/*.js.gz
gzip -dc .../dist/axe-os/28.<hash>.js.gz | grep -o 'selectedWebsiteFile\.name'
```

## 3. Factory images

```bash
# BitAxe - MUST splice the NVS config at 0x9000 or the board boot-loops
cd BitAxe-ESP-Miner
python $IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py \
  generate factory-config-gamma.csv config.bin 0x6000
./merge_bin.sh -c esp-miner-factory.bin

# NerdAxe - no config blob; merge straight after that board's build
cd ../NerdAxe-ESP-Miner && ./merge_bin.sh esp-miner-factory-nerdaxe.bin
```

All three factory images come out at exactly **15802368** bytes. A different
size means something is missing from the merge.

**A factory image is merged from whatever is in `build/` at that moment.** Do
not ship an app binary saved from an earlier build alongside a factory merged
from a later rebuild — every build embeds its own ELF SHA, so they are different
binaries under one version. Stage each app binary from the same build its
factory was merged from, and verify:

```bash
# the app must appear verbatim inside its factory image at 0x10000
python - <<'EOF'
import hashlib
app = open('esp-miner-nerdaxe.bin','rb').read()
fac = open('esp-miner-factory-nerdaxe.bin','rb').read()
print(hashlib.sha256(app).hexdigest() ==
      hashlib.sha256(fac[0x10000:0x10000+len(app)]).hexdigest())
EOF
```

## 4. Required assets

| Asset | Why |
|---|---|
| `esp-miner.bin` | BitAxe firmware (OTA) |
| `www.bin` | BitAxe web UI. Must be exactly this name — the BitAxe UI rejects any other. |
| `esp-miner-factory.bin` | BitAxe full serial flash |
| `esp-miner-nerdaxe.bin` | NerdAxe firmware (OTA) |
| `esp-miner-NerdAxeGamma.bin` | NerdAxeGamma firmware (OTA) |
| `www-nerdaxe.bin` | **NerdAxe web UI.** Added after a rollout found there was no NerdAxe www asset at all, so the only `www.bin` in the release was the BitAxe one — flashing it to a NerdAxe installs the wrong UI over NerdOS. |
| `esp-miner-factory-nerdaxe.bin` | NerdAxe full serial flash |
| `esp-miner-factory-nerdaxegamma.bin` | NerdAxeGamma full serial flash |

`www-nerdaxe.bin` is just that build's `www.bin` renamed. The NerdAxe UI accepts
`www.bin`, `www-nerdaxe.bin` and any `www-<tag>.bin` from **NerdAxe 1.5 onward**.
Devices on 1.4 or earlier still enforce the exact name `www.bin`, so a user on an
older build must update the firmware first or rename the file by hand.

## 5. Publish

```bash
gh release create <tag> <assets...> \
  --repo SerpentXSF/Dual-Pool-Mining-NerdAxe-BitAxe \
  --target <full-40-char-sha> --title "..." --notes-file notes.md
```

`--target` rejects a short SHA. Publishing matters beyond distribution: the
device's **Settings → Release & Update** panel reads the GitHub Releases API, so
until a release exists it keeps offering the previous one. The panel is pinned to
this repository in `github-update.service.ts`; if the repo is ever renamed or
moved, update that constant in the same change.

## 6. Verify on hardware before calling it done

Flash a real device and check it mines, that both pools reconnect, and that
settings survived. On a same-version reinstall the version string cannot tell
you whether the flash took — use a behavioural fingerprint instead, such as
`runningPartition` flipping between `ota_0` and `ota_1`.

**After any NerdOS OTA, restart the device.** NerdAxe and NerdQAxe come back
from an OTA running roughly 15-25% slow and stay that way; the reboot the OTA
itself performs is not enough, an explicit restart is. Nothing in the API
reports this — the only tell is the hashrate — and a device was found sitting
slow for its entire 48-day uptime because of it. Compare against the known good
rate before and after.
