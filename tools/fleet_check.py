#!/usr/bin/env python3
"""Fleet health check for SerpentX dual-pool miners.

Reads /api/system/info from each miner and prints a compact health line.
Handles both firmware families:
  * AxeOS (BitAxe)          - flat stratumURL, has errorPercentage
  * NerdOS (NerdAxe/NerdQAxe) - stratum.pools[] array, no errorPercentage

With --ws it also probes the live websocket. That check exists because a
silent-but-connected websocket is not hypothetical: the dashboard trusts an
open socket and stops polling HTTP, so a mute socket shows up to the user as
"Unable to reach the device" while the miner is perfectly healthy. A connect
test alone would have missed that - what matters is whether frames arrive.

Stdlib only, except `websockets`, which is needed only for --ws.

    python tools/fleet_check.py --scan 192.168.50
    python tools/fleet_check.py 192.168.50.243 192.168.50.144 --ws

Exits non-zero if any miner needs attention, so it can gate a release.
"""
import argparse
import json
import sys
import urllib.request
from concurrent.futures import ThreadPoolExecutor

HTTP_TIMEOUT = 6

# A miner is flagged if it trips any of these. The chip cap is the firmware's
# own overheat protection (~70C), so warn a couple of degrees below it.
WARN_TEMP_C = 68
WARN_ERROR_PCT = 5.0
WARN_REJECT_PCT = 1.0

# The dashboard declares a device unreachable after 5s without data, so any
# websocket gap approaching that is worth surfacing.
WS_GAP_WARN_MS = 5000


def fetch(ip):
    """GET /api/system/info. Returns the dict, or None if this is not a miner."""
    try:
        with urllib.request.urlopen("http://%s/api/system/info" % ip,
                                    timeout=HTTP_TIMEOUT) as r:
            d = json.load(r)
    except Exception:
        return None
    if not isinstance(d, dict) or ("hashRate" not in d and "ASICModel" not in d):
        return None
    d["_ip"] = ip
    return d


def pools_of(info):
    """Return (pool_descriptions, family).

    NerdOS keeps live pool state in stratum.pools[]; its top-level stratumURL
    can be a stale leftover, so the array wins when present. Note the
    DualPool-1.2/1.3 NerdOS build does not expose url/port in that array - only
    connection state - so those pools are described by index instead.
    """
    st = info.get("stratum")
    if isinstance(st, dict) and isinstance(st.get("pools"), list) and st["pools"]:
        out = []
        for i, p in enumerate(st["pools"]):
            if not isinstance(p, dict):
                continue
            if p.get("url"):
                where = "%s:%s" % (p.get("url"), p.get("port"))
            else:
                where = "pool[%d]" % i
            out.append({
                "where": where,
                "connected": bool(p.get("connected")),
                "accepted": p.get("accepted"),
                "rejected": p.get("rejected"),
            })
        return out, "nerdos"

    return ([{
        "where": "%s:%s" % (info.get("stratumURL"), info.get("stratumPort")),
        "connected": None,
        "accepted": info.get("sharesAccepted"),
        "rejected": info.get("sharesRejected"),
    }], "axeos")


def ws_probe(ip, family, seconds):
    """Measure real frame delivery on a miner's websocket.

    Returns (frame_count, max_gap_ms, error_or_None). A successful connect that
    delivers zero frames is the exact failure this exists to catch, so it is
    reported as a normal result (count 0), not an error.

    Family matters for how to read the result:
      * axeos  - /api/ws/live is a periodic state push, so silence is a fault.
      * nerdos - /api/ws is a LOG stream, driven by log output. A quiet device
        legitimately sends nothing, so zero frames there is informational only
        (see assess()). That socket is also single-slot: connecting to it takes
        the log stream away from anyone with the dashboard open, for the
        duration of the probe.
    """
    try:
        import asyncio
        import time
        import websockets
    except ImportError:
        return None, None, "websockets not installed (pip install websockets)"

    path = "/api/ws/live" if family == "axeos" else "/api/ws"

    # Kept outside run() so a mid-listen disconnect still reports what it saw -
    # a device that streamed briefly then dropped is a different problem from
    # one that never connected, and they should not look identical.
    stamps = []

    async def run():
        url = "ws://%s%s" % (ip, path)
        async with websockets.connect(url, open_timeout=10,
                                      ping_interval=None) as ws:
            t0 = time.time()
            while time.time() - t0 < seconds:
                try:
                    await asyncio.wait_for(ws.recv(), timeout=1)
                    stamps.append(time.time())
                except asyncio.TimeoutError:
                    pass
                except Exception:
                    break   # server closed mid-listen; keep what we counted

    err = None
    try:
        asyncio.run(run())
    except Exception as e:
        err = repr(e)

    gaps = [(stamps[i] - stamps[i - 1]) * 1000 for i in range(1, len(stamps))]
    max_gap = max(gaps) if gaps else None
    if stamps and err:
        err = None      # we got data; a late disconnect is not the headline
    return len(stamps), max_gap, err


def assess(info, ws, family="axeos"):
    """Collect the reasons this miner needs attention (empty list = healthy)."""
    warn = []

    temp = info.get("temp")
    if isinstance(temp, (int, float)) and temp >= WARN_TEMP_C:
        warn.append("temp %.0fC >= %d" % (temp, WARN_TEMP_C))

    err = info.get("errorPercentage")
    if isinstance(err, (int, float)) and err > WARN_ERROR_PCT:
        warn.append("error %.1f%% > %.1f" % (err, WARN_ERROR_PCT))

    acc, rej = info.get("sharesAccepted"), info.get("sharesRejected")
    if isinstance(acc, int) and isinstance(rej, int) and acc + rej > 0:
        pct = rej / float(acc + rej) * 100
        if pct > WARN_REJECT_PCT:
            warn.append("rejects %.2f%%" % pct)

    hr, exp = info.get("hashRate"), info.get("expectedHashrate")
    if isinstance(hr, (int, float)) and isinstance(exp, (int, float)) and exp > 0:
        if hr < exp * 0.9:
            warn.append("hashrate %.0f%% of expected" % (hr / exp * 100))

    for p in pools_of(info)[0]:
        if p["connected"] is False:
            warn.append("%s disconnected" % p["where"])

    if ws is not None:
        n, gap, err_s = ws
        if err_s:
            warn.append("ws: %s" % err_s)
        elif n == 0 and family == "axeos":
            # Only meaningful for the AxeOS state push. The NerdOS socket is a
            # log stream, so a quiet device sending nothing is not a fault and
            # must not fail a release gate.
            warn.append("ws connected but delivered NO frames")
        elif gap and gap > WS_GAP_WARN_MS and family == "axeos":
            warn.append("ws gap %.0fms > %d" % (gap, WS_GAP_WARN_MS))

    return warn


def main():
    ap = argparse.ArgumentParser(description="SerpentX fleet health check")
    ap.add_argument("ips", nargs="*", help="miner IPs (omit when using --scan)")
    ap.add_argument("--scan", metavar="PREFIX",
                    help="scan PREFIX.1-254 for miners, e.g. 192.168.50")
    ap.add_argument("--ws", action="store_true",
                    help="also probe the live websocket for actual frame delivery")
    ap.add_argument("--ws-seconds", type=int, default=10,
                    help="how long to listen on each websocket (default 10)")
    args = ap.parse_args()

    targets = list(args.ips)
    if args.scan:
        targets += ["%s.%d" % (args.scan, i) for i in range(1, 255)]
    if not targets:
        ap.error("give at least one IP or use --scan")

    with ThreadPoolExecutor(max_workers=64) as ex:
        found = [d for d in ex.map(fetch, targets) if d]
    found.sort(key=lambda d: [int(x) for x in d["_ip"].split(".")])

    if not found:
        print("No miners responded.")
        return 1

    unhealthy = 0
    for info in found:
        pools, family = pools_of(info)
        ws = ws_probe(info["_ip"], family, args.ws_seconds) if args.ws else None
        warn = assess(info, ws, family)
        if warn:
            unhealthy += 1

        hr = info.get("hashRate") or 0
        exp = info.get("expectedHashrate")
        exp_s = "/%.0f" % exp if isinstance(exp, (int, float)) else ""
        err = info.get("errorPercentage")
        err_s = " err=%.2f%%" % err if isinstance(err, (int, float)) else ""
        up_h = (info.get("uptimeSeconds") or 0) // 3600

        # temp is coerced like hr: a device reporting null must not abort the run
        temp = info.get("temp")
        temp = temp if isinstance(temp, (int, float)) else 0
        print("%s %-15s %-16s %-22s %6.0f%s GH/s %4.0fC%s up=%dh" % (
            "WARN" if warn else " ok ", info["_ip"], info.get("hostname", "?"),
            info.get("version", "?"), hr, exp_s, temp, err_s, up_h))

        for p in pools:
            if p["connected"] is None:
                state = ""
            else:
                state = "up" if p["connected"] else "DOWN"
            print("        pool %-34s %-4s acc=%s rej=%s" % (
                p["where"], state, p["accepted"], p["rejected"]))

        if ws:
            n, gap, e = ws
            gap_s = "-" if gap is None else "%.0fms" % gap
            print("        ws   frames=%s max_gap=%s%s" % (
                n, gap_s, (" " + e) if e else ""))

        for w in warn:
            print("        !! %s" % w)

    print("\n%d miner(s), %d needing attention." % (len(found), unhealthy))
    return 1 if unhealthy else 0


if __name__ == "__main__":
    sys.exit(main())
