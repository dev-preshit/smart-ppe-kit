#!/usr/bin/env python3
"""Minimal gateway stub for sprint S3 (Wi-Fi + BLE recheck 1). NOT the real gateway (that is S7+).

Accepts any GET or POST on any path, answers 200 {"ok": true} and logs one JSON line per request with the PC arrival
time, so the hub's small HTTP request every 30 s can be seen arriving. No authentication, no storage, test use only.

  python tools/gateway_stub.py --port 8080 --log test_logs/S3/gateway_stub_arrivals.log
Windows may ask to allow Python through the firewall for private networks: allow it, the hub must reach this PC.
"""
import argparse
import datetime
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

T0 = time.monotonic()


class Handler(BaseHTTPRequestHandler):
    log_file = None

    def _serve(self):
        n = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(n) if n else b""
        rec = {"arrival": datetime.datetime.now().astimezone().isoformat(timespec="milliseconds"),
               "mono_ms": int((time.monotonic() - T0) * 1000), "method": self.command, "path": self.path,
               "from": self.client_address[0], "bytes": len(body), "body": body[:200].decode(errors="replace")}
        line = json.dumps(rec)
        print(line)
        sys.stdout.flush()
        if Handler.log_file:
            with open(Handler.log_file, "a", encoding="utf-8") as f:
                f.write(line + "\n")
        reply = b'{"ok": true}'
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(reply)))
        self.end_headers()
        self.wfile.write(reply)

    do_GET = do_POST = do_PUT = _serve

    def log_message(self, fmt, *args):   # the JSON line above is the log; silence the default one
        pass


def make_server(host="0.0.0.0", port=8080, log=None):
    Handler.log_file = log
    return ThreadingHTTPServer((host, port), Handler)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--log", help="append one JSON line per request to this file")
    a = ap.parse_args(argv)
    srv = make_server(a.host, a.port, a.log)
    print("gateway_stub listening on %s:%d" % (a.host, a.port))
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
