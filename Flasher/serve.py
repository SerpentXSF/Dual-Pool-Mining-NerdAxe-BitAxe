#!/usr/bin/env python3
"""Serve the local USB flasher on http://localhost:8000 and open it.

The Web Serial API requires a *secure context*. http://localhost (and 127.0.0.1)
count as secure, so this tiny static server is all you need — do NOT open
index.html as a file:// URL, Web Serial will be blocked there.

Usage:  python serve.py            # serves this folder on :8000
        python serve.py 8080       # custom port
"""
import http.server
import socketserver
import sys
import os
import webbrowser

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
os.chdir(os.path.dirname(os.path.abspath(__file__)))


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        # No caching, so re-flashes always use the latest files.
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, fmt, *args):
        pass  # quiet


def main():
    url = f"http://localhost:{PORT}/index.html"
    with socketserver.TCPServer(("127.0.0.1", PORT), Handler) as httpd:
        print(f"Decentralized Dual Miners flasher running at {url}")
        print("Open it in Chrome or Edge. Press Ctrl+C to stop.")
        try:
            webbrowser.open(url)
        except Exception:
            pass
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nStopped.")


if __name__ == "__main__":
    main()
