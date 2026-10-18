#!/usr/bin/env python3
"""SC-02 verify_packets.py - independent HMAC-SHA256 verifier for a sniffer capture (Python hashlib only).

Input: text file, one advertisement per line: `HEX` or `TIME_US,HEX` (21-byte adv data or 17-byte payload,
spaces allowed). Lines starting with # are ignored. Export sniffer packets to this form first.
Checks: HMAC (all 8 bytes), AD header, version, reserved bits, copies per logical packet (5 identical),
copy spacing (500 +/- 50 ms when times are given), counter step between logical packets (+1).
Key: --test-key (spec Appendix A) or --key-file PATH (32 raw bytes or 64 hex chars; the key is never printed).
Exit 0 only if every packet verifies and, with --strict, the copy/spacing/counter checks also hold.
"""
import argparse, hashlib, hmac, json, sys

TEST_KEY = bytes(range(32))

def load_key(path):
    raw = open(path, "rb").read().strip()
    if len(raw) == 32: return raw
    try:
        k = bytes.fromhex(raw.decode().replace(" ", "").replace("\n", ""))
        if len(k) == 32: return k
    except Exception: pass
    raise SystemExit("key file must hold 32 raw bytes or 64 hex characters")

def parse(path):
    pk = []
    for n, line in enumerate(open(path), 1):
        line = line.strip()
        if not line or line.startswith("#"): continue
        t = None
        if "," in line: ts, line = line.split(",", 1); t = int(ts)
        b = bytes.fromhex(line.replace(" ", ""))
        pk.append((n, t, b))
    return pk

def verify(pk, key, vest=None, strict=False):
    rep = dict(total=len(pk), hmac_ok=0, hmac_fail=0, bad_format=0, bad_version=0, wrong_vest=0, reserved_set=0,
               logical=0, bad_copy_count=0, bad_spacing=0, bad_counter_step=0, details=[])
    groups = []   # [counter, node, [(t, bytes)]]
    for n, t, b in pk:
        if len(b) == 21:
            if b[:4] != bytes([0x14, 0xFF, 0xFF, 0xFF]): rep["bad_format"] += 1; rep["details"].append("line %d: bad AD header" % n); continue
            b = b[4:]
        elif len(b) != 17: rep["bad_format"] += 1; rep["details"].append("line %d: length %d" % (n, len(b))); continue
        if hmac.new(key, b[:9], hashlib.sha256).digest()[:8] == b[9:]: rep["hmac_ok"] += 1
        else: rep["hmac_fail"] += 1; rep["details"].append("line %d: HMAC mismatch" % n); continue
        if b[0] >> 6 != 1: rep["bad_version"] += 1
        if vest is not None and b[1] != vest: rep["wrong_vest"] += 1
        if b[0] & 3: rep["reserved_set"] += 1
        ctr = int.from_bytes(b[5:9], "little")
        if groups and groups[-1][0] == ctr and groups[-1][1] == b[2] and groups[-1][3] == b:
            groups[-1][2].append(t)
        else:
            if groups and groups[-1][0] == ctr and groups[-1][1] == b[2]: rep["details"].append("line %d: same counter, different bytes" % n); rep["bad_copy_count"] += 1
            groups.append([ctr, b[2], [t], b])
    rep["logical"] = len(groups)
    last = {}
    for ctr, node, ts, _ in groups:
        if len(ts) != 5: rep["bad_copy_count"] += 1; rep["details"].append("counter %d node %d: %d copies" % (ctr, node, len(ts)))
        if all(x is not None for x in ts):
            for a, c in zip(ts, ts[1:]):
                if not 450_000 <= c - a <= 550_000: rep["bad_spacing"] += 1; rep["details"].append("counter %d: spacing %d us" % (ctr, c - a))
        if node in last and ctr != last[node] + 1: rep["bad_counter_step"] += 1; rep["details"].append("node %d: counter %d after %d" % (node, ctr, last[node]))
        last[node] = ctr
    rep["ok"] = rep["hmac_fail"] == 0 and rep["bad_format"] == 0 and rep["bad_version"] == 0 and rep["wrong_vest"] == 0 and \
        (not strict or (rep["bad_copy_count"] == 0 and rep["bad_spacing"] == 0 and rep["bad_counter_step"] == 0))
    return rep

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture"); g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--test-key", action="store_true"); g.add_argument("--key-file")
    ap.add_argument("--vest", type=int); ap.add_argument("--strict", action="store_true"); ap.add_argument("--json", action="store_true")
    a = ap.parse_args(argv)
    rep = verify(parse(a.capture), TEST_KEY if a.test_key else load_key(a.key_file), a.vest, a.strict)
    if a.json: print(json.dumps(rep, indent=2))
    else:
        print("packets=%(total)d hmac_ok=%(hmac_ok)d hmac_fail=%(hmac_fail)d logical=%(logical)d bad_copy_count=%(bad_copy_count)d "
              "bad_spacing=%(bad_spacing)d bad_counter_step=%(bad_counter_step)d" % rep)
        for d in rep["details"][:20]: print("  " + d)
        print("RESULT", "PASS" if rep["ok"] else "FAIL")
    return 0 if rep["ok"] else 1

if __name__ == "__main__": sys.exit(main())
