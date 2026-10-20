#!/usr/bin/env python3
"""S3 bench injector client (test plan TC-20..TC-24, TC-32, TC-72). TEST TOOL, uses the public Appendix A key.

Two ways to deliver a packet to the hub (--via):
  injector  on air through the spare ESP32-C3 running tools/injector (needs --port of the injector)
  hub       through the hub serial hook "inject <hex>" (needs --port of the HUB; --check works only here, because this
            program then owns the hub port and can send "dump" after each packet and compare the counters)
Every packet is logged as  ISO-8601 PC time, monotonic ms, via, label, hex  to stdout and to --log.
Never run this and another program (IDE monitor, tools/s3_capture.py) on the same COM port.

Examples (python tools/inject.py ...):
  inject.py --via injector --port COM7 pkt --node 1 --ctr 1000 --copies 5     # 5 copies, 500 ms apart
  inject.py --via hub --port COM5 --check a1                                    # TC-23 valid vector
  inject.py --via hub --port COM5 --check flips                                 # TC-23, 136 bit flips
  inject.py --via hub --port COM5 --check defects --ctr 300 --malformed --repeat 20   # TC-22 step 1
  inject.py --via hub --port COM5 --check tc32                                  # TC-32 (fresh hub NVS)
  inject.py --via hub --port COM5 burst --ctr-start 5000                        # TC-72 burst of 25
  inject.py --dry-run defects --ctr 300                                         # print only, no port
"""
import argparse
import datetime
import os
import random
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_vectors as gv

DUP, REPLAY = 100, 101      # counter-rule outcomes (step 7); the gen_vectors codes only cover steps 1-6
STAGE = {DUP: "DUPLICATE", REPLAY: "REPLAY", gv.OK: "ACCEPT", gv.DROP_LEN: "DROP_LEN", gv.DROP_VER: "DROP_VER", gv.DROP_VEST: "DROP_VEST",
         gv.DROP_NODE: "DROP_NODE", gv.HMAC_FAIL: "HMAC_FAIL", gv.DROP_STATE: "DROP_STATE"}
GCOUNTERS = ["rx_total", "drop_len", "drop_ver", "drop_vest", "drop_node", "drop_state"]
NCOUNTERS = ["accepted", "dup", "replay", "hmac_fail"]


def now_iso():
    return datetime.datetime.now().astimezone().isoformat(timespec="milliseconds")


# ------------------------------------------------------------ packet builders (independent of the firmware)
def signed(node, ctr, state=1, batt=90, vest=1, flags=0x60):
    return gv.adv(gv.payload(flags, vest, node, state, batt, ctr))


def defect_cases(ctr, node=3, malformed=False):
    """(label, bytes, expected stage). Same defects as tools/gen_vectors.py; expectations follow the spec order."""
    def base(**k):
        return gv.payload(k.get("flags", 0x60), k.get("vest", 1), k.get("node", node), k.get("state", 1), 90, ctr)
    cases = []
    if malformed:
        good = gv.adv(base())
        cases += [("len20", good[:20], gv.DROP_LEN), ("len22", good + b"\x00", gv.DROP_LEN), ("len0", b"", gv.DROP_LEN)]
        for i, nm in enumerate(["adlen", "adtype", "cid_lo", "cid_hi"]):
            d = bytearray(good)
            d[i] ^= 1
            cases.append(("hdr_" + nm, bytes(d), gv.DROP_LEN))
    for v in (0, 2, 3):
        cases.append(("ver%d" % v, gv.adv(gv.payload((v << 6) | 0x20, 1, node, 1, 90, ctr)), gv.DROP_VER))
    cases.append(("wrong_vest", gv.adv(gv.payload(0x60, 2, node, 1, 90, ctr)), gv.DROP_VEST))
    for n in (0, 6, 0xFF):
        cases.append(("node%d" % n, gv.adv(gv.payload(0x60, 1, n, 1, 90, ctr)), gv.DROP_NODE))
    bad = bytearray(gv.adv(base()))
    bad[-1] ^= 1
    cases.append(("bad_hmac", bytes(bad), gv.HMAC_FAIL))
    cases.append(("state02_valid_hmac", gv.adv(gv.payload(0x60, 1, node, 2, 90, ctr)), gv.DROP_STATE))
    cases.append(("state_ff_valid_hmac", gv.adv(gv.payload(0x60, 1, node, 0xFF, 90, ctr)), gv.DROP_STATE))
    cases.append(("two_ver_vest", gv.adv(gv.payload(0x20, 2, node, 1, 90, ctr)), gv.DROP_VER))
    bad = bytearray(gv.adv(gv.payload(0x60, 1, 6, 1, 90, ctr)))
    bad[-1] ^= 1
    cases.append(("two_node_hmac", bytes(bad), gv.DROP_NODE))
    bad = bytearray(gv.adv(gv.payload(0x60, 1, node, 2, 90, ctr)))
    bad[-1] ^= 1
    cases.append(("two_state_hmac", bytes(bad), gv.HMAC_FAIL))
    cases.append(("valid_control", gv.adv(base()), gv.OK))
    return cases


def flip_cases():
    a1, _ = gv.self_check()
    out = []
    for byte in range(17):
        for bit in range(8):
            q = bytearray(a1)
            q[byte] ^= 1 << bit
            q = bytes(q)
            out.append(("flip_b%d_bit%d" % (byte, bit), gv.adv(q), gv.expected_stage(q)))
    return out


def forged_cases(n):
    rnd = random.Random(0x5EED1)
    out = []
    while len(out) < n:
        p = bytes([0x60, 1, rnd.randint(1, 5), rnd.randint(0, 1), rnd.randint(0, 100)]) \
            + rnd.getrandbits(32).to_bytes(4, "little") + rnd.randbytes(8)
        if gv.tag(p[:9]) == p[9:]:
            continue
        out.append(("forged%d" % len(out), gv.adv(p), gv.HMAC_FAIL))
    return out


# ------------------------------------------------------------ hub dump parsing and counter checks
def parse_dump(lines):
    snap = {"g": {}, "n": {}}
    for line in lines:
        m = re.search(r"DUMP global (.*)", line)
        if m:
            snap["g"] = {k: int(v) for k, v in re.findall(r"(\w+)=(\d+)", m.group(1))}
        m = re.search(r"DUMP node=(\d) state=(\w+) last_counter=(\d+) last_seen_ms=(\d+) accepted=(\d+) dup=(\d+) "
                      r"replay=(\d+) gap=(\d+) hmac_fail=(\d+)", line)
        if m:
            i = int(m.group(1))
            snap["n"][i] = dict(state=m.group(2), last_counter=int(m.group(3)), last_seen_ms=int(m.group(4)),
                                accepted=int(m.group(5)), dup=int(m.group(6)), replay=int(m.group(7)),
                                gap=int(m.group(8)), hmac_fail=int(m.group(9)))
    return snap


def diff(before, after):
    d = {}
    for k in GCOUNTERS:
        if after["g"].get(k, 0) != before["g"].get(k, 0):
            d["g." + k] = after["g"][k] - before["g"][k]
    for i in after["n"]:
        for k in NCOUNTERS:
            a, b = after["n"][i][k], before["n"][i][k]
            if a != b:
                d["n%d.%s" % (i, k)] = a - b
    return d


def expected_diff(stage, pkt):
    """Only the first matching counter rises (TC-22). The node id is adv byte 6 (payload byte 2)."""
    d = {"g.rx_total": 1}
    if stage == gv.DROP_LEN:
        d["g.drop_len"] = 1
    elif stage == gv.DROP_VER:
        d["g.drop_ver"] = 1
    elif stage == gv.DROP_VEST:
        d["g.drop_vest"] = 1
    elif stage == gv.DROP_NODE:
        d["g.drop_node"] = 1
    elif stage == gv.DROP_STATE:
        d["g.drop_state"] = 1
    elif stage == gv.HMAC_FAIL:
        d["n%d.hmac_fail" % pkt[6]] = 1
    elif stage == gv.OK:
        d["n%d.accepted" % pkt[6]] = 1
    elif stage == DUP:
        d["n%d.dup" % pkt[6]] = 1
    elif stage == REPLAY:
        d["n%d.replay" % pkt[6]] = 1
    return d


def tc32_cases(node=2):
    """TC-32 step 1: known numbers of each kind. Needs node `node` at last_counter 0 (erase the hub NVS first).
    10 bad length, 7 wrong version, 12 wrong vest, 9 unknown node, 15 bad HMAC, 6 valid HMAC + invalid state,
    20 duplicates, 8 replays, 30 accepted."""
    good = signed(node, 1)
    cases = []
    for i in range(30):
        cases.append(("accepted c%d" % (i + 1), signed(node, i + 1), gv.OK))
    for i in range(20):
        cases.append(("duplicate c30 #%d" % (i + 1), signed(node, 30), DUP))
    for i in range(8):
        cases.append(("replay c%d" % (i + 1), signed(node, i + 1), REPLAY))
    for i in range(5):
        cases.append(("bad_len20 #%d" % (i + 1), good[:20], gv.DROP_LEN))
        cases.append(("bad_len22 #%d" % (i + 1), good + b"\x00", gv.DROP_LEN))
    for i, v in enumerate([0, 2, 3, 0, 2, 3, 0]):
        cases.append(("wrong_version%d #%d" % (v, i + 1), gv.adv(gv.payload((v << 6) | 0x20, 1, node, 1, 90, 40)), gv.DROP_VER))
    for i in range(12):
        cases.append(("wrong_vest #%d" % (i + 1), gv.adv(gv.payload(0x60, 2 + i, node, 1, 90, 40)), gv.DROP_VEST))
    for i, n in enumerate([0, 6, 0xFF] * 3):
        cases.append(("unknown_node%d #%d" % (n, i + 1), gv.adv(gv.payload(0x60, 1, n, 1, 90, 40)), gv.DROP_NODE))
    for i in range(15):
        bad = bytearray(signed(node, 41 + i))
        bad[-1 - (i % 8)] ^= 0x01
        cases.append(("bad_hmac #%d" % (i + 1), bytes(bad), gv.HMAC_FAIL))
    for i, st in enumerate([2, 0xFF, 3, 0x80, 2, 0xFF]):
        cases.append(("invalid_state%02X #%d" % (st, i + 1), gv.adv(gv.payload(0x60, 1, node, st, 90, 40)), gv.DROP_STATE))
    return cases


def tc32_totals(node=2):
    """What the dump must show after tc32_cases (rx_total 117)."""
    return {"g.rx_total": 117, "g.drop_len": 10, "g.drop_ver": 7, "g.drop_vest": 12, "g.drop_node": 9, "g.drop_state": 6,
            "n%d.accepted" % node: 30, "n%d.dup" % node: 20, "n%d.replay" % node: 8, "n%d.hmac_fail" % node: 15}


# ------------------------------------------------------------ links
class Out:
    def __init__(self, via, path=None):
        self.via = via
        self.f = open(path, "a", encoding="utf-8") if path else None
        self.t0 = time.monotonic()

    def packet(self, label, data):
        line = "%s,%d,%s,%s,%s" % (now_iso(), int((time.monotonic() - self.t0) * 1000), self.via, label, data.hex())
        print(line)
        sys.stdout.flush()
        if self.f:
            self.f.write(line + "\n")
            self.f.flush()

    def note(self, text):
        print(text)
        sys.stdout.flush()
        if self.f:
            self.f.write("# " + text + "\n")
            self.f.flush()


class InjectorLink:
    def __init__(self, port, baud=115200, copies=1, spacing_ms=500):
        import serial
        self.s = serial.Serial(port, baud, timeout=0.2)
        self.copies, self.spacing = copies, spacing_ms
        time.sleep(0.3)
        self.s.reset_input_buffer()

    def send(self, data):
        self.s.write(("ADV %s %d %d\n" % (data.hex(), self.copies, self.spacing)).encode())
        deadline = time.time() + self.copies * self.spacing / 1000.0 + 5
        buf = ""
        while time.time() < deadline:
            buf += self.s.read(200).decode(errors="replace")
            if "DONE" in buf:
                return True
            if "ERR" in buf:
                print("injector:", buf.strip())
                return False
        print("injector: no DONE reply")
        return False


class HubLink:
    def __init__(self, port, baud=115200, copies=1, spacing_ms=0):
        import serial
        self.s = serial.Serial(port, baud, timeout=0.2)
        self.copies, self.spacing = copies, spacing_ms
        time.sleep(0.3)
        self.s.reset_input_buffer()

    def send(self, data):
        for i in range(self.copies):
            self.s.write(("inject %s\n" % data.hex()).encode())
            if self.spacing and i + 1 < self.copies:
                time.sleep(self.spacing / 1000.0)
        return True

    def write_batch(self, lines):
        self.s.write("".join(line + "\n" for line in lines).encode())

    def snapshot(self, timeout=3.0):
        self.s.write(b"dump\n")
        lines = []
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = self.s.readline().decode(errors="replace").strip()
            if line.startswith("DUMP"):
                lines.append(line)
            if line == "DUMP end":
                self.last_raw = lines
                return parse_dump(lines)
        raise RuntimeError("no complete DUMP from the hub (PPE_TEST_HOOKS build? port free?)")

    def command(self, line):
        self.s.write((line + "\n").encode())
        time.sleep(0.3)

    def settle(self, seconds=0.15):
        time.sleep(seconds)


# ------------------------------------------------------------ runners
def run_set(link, out, cases, check, settle=0.15):
    """Send every case; with --check (hub link) compare the counter delta to the expectation. Returns failures."""
    fails = 0
    snap = link.snapshot() if check else None
    for label, data, stage in cases:
        out.packet("%s expect=%s" % (label, STAGE[stage]), data)
        link.send(data)
        if check:
            link.settle(settle)
            after = link.snapshot()
            if len(data) < 7:
                want = {"g.rx_total": 1, "g.drop_len": 1}
            else:
                want = expected_diff(stage, data)
            got = diff(snap, after)
            ok = (got == want)
            out.note("%s %s: got %s want %s" % ("PASS" if ok else "FAIL", label, got, want))
            fails += 0 if ok else 1
            snap = after
    return fails


def build_parser():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--via", choices=["injector", "hub"], default="injector")
    ap.add_argument("--port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--log")
    ap.add_argument("--dry-run", action="store_true", help="print the packets, open no port")
    ap.add_argument("--check", action="store_true", help="hub link only: compare counters after every packet")
    ap.add_argument("--pre", action="append", help="hub link only: serial hook command sent first, e.g. --pre \"time off\"")
    ap.add_argument("--dump-at-end", action="store_true", help="hub link only: write the raw DUMP lines to the log at the end")
    sub = ap.add_subparsers(dest="cmd", required=True)

    def common(p):
        p.add_argument("--copies", type=int, default=1)
        p.add_argument("--spacing-ms", type=int, default=None)

    p = sub.add_parser("hex")
    p.add_argument("hexes", nargs="+")
    common(p)
    p = sub.add_parser("pkt")
    p.add_argument("--node", type=int, required=True)
    p.add_argument("--ctr", type=int, required=True)
    p.add_argument("--state", type=int, default=1)
    p.add_argument("--batt", type=int, default=90)
    p.add_argument("--vest", type=int, default=1)
    p.add_argument("--flags", type=lambda x: int(x, 0), default=0x60)
    common(p)
    p = sub.add_parser("seq")
    p.add_argument("--node", type=int, required=True)
    p.add_argument("--ctr-start", type=int, required=True)
    p.add_argument("--count", type=int, required=True)
    p.add_argument("--interval-ms", type=int, default=0)
    common(p)
    p = sub.add_parser("a1")
    common(p)
    p = sub.add_parser("flips")
    common(p)
    p = sub.add_parser("defects")
    p.add_argument("--ctr", type=int, required=True)
    p.add_argument("--node", type=int, default=3)
    p.add_argument("--malformed", action="store_true", help="also bad length / bad AD header")
    p.add_argument("--repeat", type=int, default=1, help="TC-22 step 1: send each defect this many times (20)")
    common(p)
    p = sub.add_parser("tc32")
    p.add_argument("--node", type=int, default=2)
    common(p)
    p = sub.add_parser("forged")
    p.add_argument("n", type=int, nargs="?", default=1000)
    common(p)
    p = sub.add_parser("burst")
    p.add_argument("--ctr-start", type=int, required=True)
    p.add_argument("--nodes", default="1,2,3,4,5")
    p.add_argument("--copies", type=int, default=5)
    return ap


def main(argv=None):
    ap = build_parser()
    a = ap.parse_args(argv)
    spacing = getattr(a, "spacing_ms", None)
    spacing = (500 if a.via == "injector" else 0) if spacing is None else spacing
    copies = getattr(a, "copies", 1)
    out = Out("dry" if a.dry_run else a.via, a.log)
    link = None
    if a.check and (a.via != "hub" or a.dry_run):
        ap.error("--check needs --via hub and a real port")
    if not a.dry_run:
        if not a.port:
            ap.error("--port is required unless --dry-run")
        link = (InjectorLink if a.via == "injector" else HubLink)(a.port, a.baud, copies, spacing)

    if link and a.via == "hub":
        for c in a.pre or []:
            link.command(c)
            out.note("sent hook command: %s" % c)
    if a.cmd == "burst":
        lines = []
        for n in [int(x) for x in a.nodes.split(",")]:
            for c in range(a.copies):
                lines.append(("burst n%d copy%d" % (n, c + 1), signed(n, a.ctr_start + n)))
        for label, d in lines:
            out.packet(label, d)
        if link:
            link.write_batch(["inject %s" % d.hex() for _, d in lines])
            out.note("burst of %d packets written in one go; now run dump: rx_total delta must be %d, queue_full 0"
                     % (len(lines), len(lines)))
            if a.dump_at_end and a.via == "hub":
                time.sleep(1.0)
                link.snapshot()
                for l in link.last_raw:
                    out.note(l)
        return 0

    if a.cmd == "hex":
        cases = [("hex", bytes.fromhex(h.replace(":", "").replace(" ", "")), gv.OK) for h in a.hexes]
    elif a.cmd == "pkt":
        cases = [("pkt", signed(a.node, a.ctr, a.state, a.batt, a.vest, a.flags), gv.OK)]
    elif a.cmd == "seq":
        cases = [("seq n%d c%d" % (a.node, a.ctr_start + i), signed(a.node, a.ctr_start + i), gv.OK)
                 for i in range(a.count)]
    elif a.cmd == "a1":
        cases = [("A1", gv.adv(gv.self_check()[0]), gv.OK)]
    elif a.cmd == "flips":
        cases = flip_cases()
    elif a.cmd == "defects":
        cases = []
        for label, d, st in defect_cases(a.ctr, a.node, a.malformed):
            for i in range(1 if st == gv.OK else max(a.repeat, 1)):   # the valid control is sent once (a repeat is a duplicate)
                cases.append((label if a.repeat <= 1 else "%s #%d" % (label, i + 1), d, st))
    elif a.cmd == "tc32":
        cases = tc32_cases(a.node)
    else:
        cases = forged_cases(a.n)

    fails = 0
    if a.dry_run:
        for label, d, st in cases:
            out.packet("%s expect=%s" % (label, STAGE[st]), d)
    elif a.cmd == "seq" and a.interval_ms:
        for label, d, st in cases:
            out.packet(label, d)
            link.send(d)
            time.sleep(a.interval_ms / 1000.0)
    else:
        fails = run_set(link, out, cases, a.check)
    if a.dump_at_end and link and a.via == "hub":
        time.sleep(1.0)
        link.snapshot()
        for l in link.last_raw:
            out.note(l)
    if a.check and a.cmd == "tc32" and link:
        snap = link.snapshot()
        got = {}
        for k in GCOUNTERS:
            got["g." + k] = snap["g"].get(k, 0)
        n = snap["n"][a.node]
        for k in ("accepted", "dup", "replay", "hmac_fail"):
            got["n%d.%s" % (a.node, k)] = n[k]
        want = tc32_totals(a.node)
        ok = all(got.get(k) == v for k, v in want.items())
        out.note("%s TC-32 totals: got %s want %s" % ("PASS" if ok else "FAIL", got, want))
        fails += 0 if ok else 1
    if a.check:
        out.note("RESULT %s (%d of %d packets failed)" % ("PASS" if fails == 0 else "FAIL", fails, len(cases)))
        if a.cmd == "forged" and link:
            snap = link.snapshot()
            out.note("forged: hmac_fail per node = %s" % {i: snap["n"][i]["hmac_fail"] for i in snap["n"]})
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
