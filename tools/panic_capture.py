#!/usr/bin/env python3
"""Catch a miner's crash log before the ring buffer eats it.

Three BitAxes have now panicked simultaneously twice, and both times the
backtrace was gone by the time anyone looked: AxeOS keeps roughly 23 minutes
of log in a RAM ring buffer, and the panics were found hours later. The logs
DO survive a soft reboot (see log_buffer.c, "Soft reboot detected, N bytes of
logs preserved") - they just get overwritten by ordinary mining chatter soon
after.

So this polls uptime, and the moment a device's uptime goes BACKWARDS it grabs
the whole log immediately, plus a state snapshot, into a timestamped file.

    python tools/panic_capture.py 192.168.50.149 192.168.50.218 192.168.50.243

Stdlib only. Safe to leave running for days; it only reads.
"""
import argparse
import json
import os
import sys
import time
import urllib.request
from datetime import datetime

POLL_S = 30          # detect a reboot within this long; buffer holds ~23 min
HTTP_TIMEOUT = 8
LOG_MAX = 4_000_000  # don't let one capture run away


def get_json(ip, path="/api/system/info"):
    with urllib.request.urlopen("http://%s%s" % (ip, path), timeout=HTTP_TIMEOUT) as r:
        return json.load(r)


def get_text(ip, path):
    with urllib.request.urlopen("http://%s%s" % (ip, path), timeout=30) as r:
        return r.read(LOG_MAX).decode("utf-8", "replace")


def capture(ip, info, outdir, why):
    """Dump everything we can before the evidence rots."""
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    host = (info or {}).get("hostname", "unknown")
    base = os.path.join(outdir, "panic_%s_%s_%s" % (host, ip.replace(".", "-"), stamp))
    try:
        logs = get_text(ip, "/api/system/logs")
    except Exception as e:
        logs = "<<log fetch failed: %r>>" % (e,)
    with open(base + ".log", "w", encoding="utf-8") as f:
        f.write("captured %s\nreason: %s\nip: %s\n%s\n\n" % (stamp, why, ip, "-" * 60))
        f.write(logs)
    with open(base + ".json", "w", encoding="utf-8") as f:
        json.dump(info or {}, f, indent=2)
    return base


def main():
    ap = argparse.ArgumentParser(description="Capture miner logs the moment a reboot is detected")
    ap.add_argument("ips", nargs="+")
    ap.add_argument("--outdir", default="panic-captures")
    ap.add_argument("--poll", type=int, default=POLL_S)
    args = ap.parse_args()
    os.makedirs(args.outdir, exist_ok=True)

    last = {}
    events = os.path.join(args.outdir, "events.txt")

    def note(msg):
        line = "%s  %s" % (datetime.now().strftime("%Y-%m-%d %H:%M:%S"), msg)
        print(line, flush=True)
        with open(events, "a", encoding="utf-8") as f:
            f.write(line + "\n")

    note("watching %s every %ds" % (", ".join(args.ips), args.poll))
    while True:
        for ip in args.ips:
            try:
                d = get_json(ip)
            except Exception:
                # Unreachable is not itself proof of a reboot - the device may
                # just be busy or the wifi flaky. Wait for it to come back and
                # let the uptime comparison decide.
                continue
            up = d.get("uptimeSeconds") or 0
            prev = last.get(ip)
            if prev is not None and up < prev:
                reason = d.get("resetReason") or d.get("lastResetReason") or "?"
                base = capture(ip, d, args.outdir, "uptime %ds -> %ds | %s" % (prev, up, reason))
                note("REBOOT %s (%s): %ds -> %ds | %s -> %s" % (
                    ip, d.get("hostname"), prev, up, reason, os.path.basename(base) + ".log"))
            last[ip] = up
        time.sleep(args.poll)


if __name__ == "__main__":
    sys.exit(main())
