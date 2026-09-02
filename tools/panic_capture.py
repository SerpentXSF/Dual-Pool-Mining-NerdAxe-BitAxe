#!/usr/bin/env python3
"""Catch a miner's crash log before it is gone.

Miners here reboot without warning - a stratum outage makes the firmware stop
mining and restart - and by the time anyone looks, the evidence has rotted.
How it rots differs by firmware, and that difference drives this whole tool:

  AxeOS (BitAxe)     keeps ~23 minutes of log in a RAM ring buffer and it
                     SURVIVES a soft reboot (log_buffer.c: "Soft reboot
                     detected, N bytes of logs preserved"). Fetching
                     /api/system/logs promptly after a reboot therefore still
                     yields the pre-crash lines. That is how the 2026-09-01
                     GammaX4 panic was traced to a stratum failure.

  NerdOS (NerdAxe,   has NO log endpoint at all. /api/system/logs returns 404
  NerdQAxe)          and the only log path is the /api/ws websocket, a LIVE
                     stream that retains nothing. Once the miner reboots its
                     pre-crash log is gone; no amount of polling afterwards
                     brings it back.

So on NerdOS the log has to be captured BEFORE the crash or not at all, which
means holding the websocket open and buffering it. That is opt-in (--stream)
and deliberately not the default: NerdOS serves exactly one log client
(`int websocket_fd = -1` in http_websocket.cpp), so streaming takes the log
view away from that device's own dashboard for as long as this runs.

    python tools/panic_capture.py 192.168.50.149 192.168.50.243      # AxeOS
    python tools/panic_capture.py 192.168.50.210 --stream            # NerdOS

Stdlib only, except `websockets`, which is needed only for --stream. Read-only:
it never writes to a miner.
"""
import argparse
import json
import os
import sys
import threading
import time
import urllib.error
import urllib.request
from collections import deque
from datetime import datetime

POLL_S = 30
HTTP_TIMEOUT = 8
LOG_MAX = 4000000
STREAM_LINES = 4000       # per-device ring held in memory for NerdOS


def get_json(ip, path="/api/system/info"):
    with urllib.request.urlopen("http://%s%s" % (ip, path), timeout=HTTP_TIMEOUT) as r:
        return json.load(r)


def get_text(ip, path, timeout=30):
    with urllib.request.urlopen("http://%s%s" % (ip, path), timeout=timeout) as r:
        return r.read(LOG_MAX).decode("utf-8", "replace")


def detect_family(ip):
    """Return 'axeos' if the miner can serve logs over HTTP, else 'nerdos'.

    Probed once at startup rather than inferred from hostname or branding,
    because what matters here is only whether a log ENDPOINT exists.
    """
    try:
        urllib.request.urlopen("http://%s/api/system/logs" % ip, timeout=HTTP_TIMEOUT).read(64)
        return "axeos"
    except urllib.error.HTTPError as e:
        return "nerdos" if e.code == 404 else "axeos"
    except Exception:
        return "unknown"


class NerdosStreamer(threading.Thread):
    """Hold a NerdOS log websocket open, keeping the last STREAM_LINES lines.

    NerdOS retains nothing across a reboot, so this buffer is the only chance of
    ever seeing what one said on its way down.
    """

    def __init__(self, ip):
        super().__init__(daemon=True)
        self.ip = ip
        self.buf = deque(maxlen=STREAM_LINES)
        self.lock = threading.Lock()
        self.connected = False

    def snapshot(self):
        with self.lock:
            return list(self.buf)

    def run(self):
        import asyncio
        try:
            import websockets
        except ImportError:
            with self.lock:
                self.buf.append("<<--stream needs the 'websockets' package: pip install websockets>>")
            return

        async def pump():
            while True:
                try:
                    async with websockets.connect("ws://%s/api/ws" % self.ip,
                                                  open_timeout=10, ping_interval=None) as ws:
                        self.connected = True
                        while True:
                            msg = await ws.recv()
                            with self.lock:
                                self.buf.append("%s %s" % (
                                    datetime.now().strftime("%H:%M:%S"), str(msg).rstrip()))
                except Exception:
                    # A reboot is the expected way this drops. Wait, reconnect, and
                    # keep whatever the miner said on the way down.
                    self.connected = False
                    await asyncio.sleep(5)

        asyncio.run(pump())


def capture(ip, info, outdir, why, family, streamer=None):
    """Write everything obtainable about this reboot, and be explicit about gaps."""
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    host = (info or {}).get("hostname", "unknown")
    safe_host = "".join(c if c.isalnum() or c in "-_" else "_" for c in str(host))
    base = os.path.join(outdir, "panic_%s_%s_%s" % (safe_host, ip.replace(".", "-"), stamp))

    if family == "axeos":
        try:
            body = get_text(ip, "/api/system/logs")
            source = "GET /api/system/logs (AxeOS ring buffer, preserved across a soft reboot)"
        except Exception as e:
            body = "<<log fetch failed: %r>>" % (e,)
            source = "GET /api/system/logs FAILED"
    elif streamer is not None:
        lines = streamer.snapshot()
        body = "\n".join(lines) if lines else "<<streamer buffer empty - did it connect?>>"
        source = "buffered from ws://%s/api/ws (%d lines held before the reboot)" % (ip, len(lines))
    else:
        body = ("<<no log available>>\n\n"
                "This is a NerdOS device. It exposes no HTTP log endpoint\n"
                "(/api/system/logs returns 404) and retains nothing across a\n"
                "reboot, so its pre-crash log cannot be recovered after the fact.\n"
                "Re-run with --stream to buffer this device continuously.\n"
                "Doing so takes the log view away from its dashboard, because\n"
                "NerdOS serves only one websocket log client.\n")
        source = "unavailable (NerdOS, --stream not enabled)"

    with open(base + ".log", "w", encoding="utf-8") as f:
        f.write("captured  : %s\nreason    : %s\nip        : %s\nfirmware  : %s\nlog source: %s\n%s\n\n"
                % (stamp, why, ip, family, source, "-" * 70))
        f.write(body)
    with open(base + ".json", "w", encoding="utf-8") as f:
        json.dump(info or {}, f, indent=2)
    return base


def main():
    ap = argparse.ArgumentParser(description="Capture miner logs the moment a reboot is detected")
    ap.add_argument("ips", nargs="+")
    ap.add_argument("--outdir", default="panic-captures")
    ap.add_argument("--poll", type=int, default=POLL_S)
    ap.add_argument("--stream", action="store_true",
                    help="Hold the NerdOS log websocket open and buffer it, so a NerdOS "
                         "crash log can be captured at all. Takes the log view away from "
                         "that device's dashboard while running.")
    args = ap.parse_args()
    os.makedirs(args.outdir, exist_ok=True)
    events = os.path.join(args.outdir, "events.txt")

    def note(msg):
        line = "%s  %s" % (datetime.now().strftime("%Y-%m-%d %H:%M:%S"), msg)
        print(line, flush=True)
        with open(events, "a", encoding="utf-8") as f:
            f.write(line + "\n")

    family = {ip: detect_family(ip) for ip in args.ips}
    streamers = {}
    for ip, fam in family.items():
        if fam == "nerdos" and args.stream:
            s = NerdosStreamer(ip)
            s.start()
            streamers[ip] = s

    note("watching %s every %ds" % (", ".join("%s(%s%s)" % (
        ip, family[ip], "+stream" if ip in streamers else "") for ip in args.ips), args.poll))
    for ip, fam in family.items():
        if fam == "nerdos" and not args.stream:
            note("NOTE %s is NerdOS: it keeps no retrievable log, so a crash log cannot "
                 "be captured after the fact. Use --stream to buffer it." % ip)

    last = {}
    while True:
        for ip in args.ips:
            try:
                d = get_json(ip)
            except Exception:
                continue          # unreachable != rebooted; let uptime decide
            up = d.get("uptimeSeconds") or 0
            prev = last.get(ip)
            if prev is not None and up < prev:
                reason = d.get("resetReason") or d.get("lastResetReason") or "?"
                base = capture(ip, d, args.outdir,
                               "uptime %ds -> %ds | %s" % (prev, up, reason),
                               family.get(ip, "unknown"), streamers.get(ip))
                note("REBOOT %s (%s): %ds -> %ds | %s -> %s" % (
                    ip, d.get("hostname"), prev, up, reason, os.path.basename(base) + ".log"))
            last[ip] = up
        time.sleep(args.poll)


if __name__ == "__main__":
    sys.exit(main())
