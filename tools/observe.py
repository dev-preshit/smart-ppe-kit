#!/usr/bin/env python3
"""observe.py - S4 observer tool (A-14): capture from tools/observer, burst analysis, injector calibration, result row.
All times come from the observer's own esp_timer clock (PKT and TIME lines); the PC time only orders lines (A-01).

  capture   --port COM6 --tc TC-10 --seconds 60 [--filter 21] [--waived] [--header TEXT]
            [--node COM9 --node-send "tx worn" --node-after 5]   (the node's own lines are logged as NODE ..., A-20)
            [--injector COM4 --adv "<hex> <copies> <spacing_ms>" --adv-after 3]   (calibration: the injector sends known bursts)
  analyze   --tc TC-10 [--log PATH] [--copies 5] [--spacing 500] [--tol 50] [--quiet 10] [--expect-tx N] [--need-marks] [--node-id N]
            [--len 21] [--prefix 14ffffff] [--gap 0.2]       one 'copy' = one burst of PDUs (A-15); exit 0 = PASS, 1 = FAIL
  calibrate --tc OBS-CAL [--log PATH] --spacing 500        measured spacing error of the observer against the injector (no verdict)
  plan      --out plan.txt [--cycles 20 --hold 6 --grace 2 --hb 60 --heartbeats 20]   timed hook lines for a FOOT run (adc hook)
  result    --tc TC-10 --result PASS|FAIL|BLOCKED          one row in test_logs/summary_s4.csv

Log: test_logs/S4/<TC>_<sha7>.log. A log is never appended to by a second capture. Never run two programs on one COM port."""
import argparse, os, re, statistics, sys, threading, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import logger

SPRINT = "S4"
PKT = re.compile(r"\| PKT (\d+)\.(\d{6}) (\d+) (\d+) (-?\d+) ([0-9a-f]*)\s*$")
TIME = re.compile(r"\| TIME (\d+)\.(\d{6})\s*$")
LOSS = re.compile(r"\| (?:LOST (\d+)|STAT .*lost=(\d+))")
NODE_MARK = re.compile(r"\| NODE MARK COPY(\d) (\d+)")
NODE_TX = re.compile(r"\| NODE .*TX seq=(\d+) type=(event|heartbeat) ctr=(\d+) state=(\d+) boot=(\d) fault=(\d)")


def log_file(a):
    return a.log or logger.log_path(a.tc, SPRINT)


def parse(path):
    """-> (pdus [(us, len, evt, rssi, hex)], end_us, lost). end_us = last PKT or TIME time of the observer clock."""
    pk, end, lost = [], 0, 0
    with open(path, encoding="utf-8") as f:
        for ln in f:
            if "| OK reset" in ln:      # a capture starts with RESET: whatever came before it does not count
                pk, end, lost = [], 0, 0
                continue
            m = PKT.search(ln)
            if m:
                us = int(m.group(1)) * 1000000 + int(m.group(2))
                pk.append((us, int(m.group(3)), int(m.group(4)), int(m.group(5)), m.group(6))); end = max(end, us); continue
            m = TIME.search(ln)
            if m:
                end = max(end, int(m.group(1)) * 1000000 + int(m.group(2))); continue
            m = LOSS.search(ln)
            if m:
                lost = int(m.group(1) or m.group(2))
    return pk, end, lost


def parse_node(path):
    """Lines the node printed during the capture: its copy-start marks (own micros clock) and its TX lines."""
    seqs, cur, tx = [], None, []
    with open(path, encoding="utf-8") as f:
        for ln in f:
            m = NODE_MARK.search(ln)
            if m:
                k, us = int(m.group(1)), int(m.group(2))
                if k == 0:
                    cur = []; seqs.append(cur)
                if cur is not None:
                    cur.append((k, us))
                continue
            m = NODE_TX.search(ln)
            if m:
                tx.append((int(m.group(1)), m.group(2), int(m.group(3)), int(m.group(4)), int(m.group(5)), int(m.group(6))))
    return seqs, tx


def bursts(pdus, gap_us):
    out = []
    for p in sorted(pdus):
        if out and p[0] - out[-1][-1][0] <= gap_us:
            out[-1].append(p)
        else:
            out.append([p])
    return out


def transmissions(pdus, gap_us):
    """Identical bytes = one transmission (the copies share one counter). -> [(hex, [burst, ...])] by first time."""
    by = {}
    for p in pdus:
        by.setdefault(p[4], []).append(p)
    tx = [(h, bursts(v, gap_us)) for h, v in by.items()]
    tx.sort(key=lambda t: t[1][0][0][0])
    return tx


def decode(h):
    b = bytes.fromhex(h)
    if len(b) != 21:
        return "len=%d" % len(b)
    fl = b[4]
    return "node=%d vest=%d type=%s flags=0x%02x boot=%d low_batt=%d fault=%d state=%d batt=%d ctr=%d" % (
        b[6], b[5], "event" if fl & 0x20 else "heartbeat", fl, (fl >> 4) & 1, (fl >> 3) & 1, (fl >> 2) & 1,
        b[7], b[8], int.from_bytes(b[9:13], "little"))


def dec(h):
    b = bytes.fromhex(h)
    return {"event": bool(b[4] & 0x20), "boot": bool(b[4] & 0x10), "fault": bool(b[4] & 0x04), "state": b[7], "node": b[6],
            "ctr": int.from_bytes(b[9:13], "little")}


def spacings_ms(bs):
    return [(bs[i][0][0] - bs[i - 1][0][0]) / 1000.0 for i in range(1, len(bs))]


def emit(path, lines):
    with open(path, "a", encoding="utf-8", newline="\n") as f:
        for ln in lines:
            f.write("# ANALYSIS %s\n" % ln)
            print(ln)


def cmd_analyze(a):
    path = log_file(a)
    pk, end, lost = parse(path)
    pdus = [p for p in pk if p[1] == a.len and p[4].startswith(a.prefix)]
    tx = transmissions(pdus, int(a.gap * 1000000))
    errs, out = [], ["analyze %s: observer PDUs=%d, matching %d-byte PDUs=%d, transmissions=%d, lost=%d" % (
        os.path.basename(path), len(pk), a.len, len(pdus), len(tx), lost)]
    if not tx and a.expect_tx != 0:                  # --expect-tx 0 = TC-01 step 5: an unprovisioned board must send nothing
        errs.append("no matching transmission in the log")
    if lost:
        errs.append("observer queue overflow, lost=%d" % lost)
    wrong = [p for p in pdus if p[2] != 3]
    if wrong:
        errs.append("%d PDUs are not ADV_NONCONN_IND (evt 3), first evt=%d" % (len(wrong), wrong[0][2]))
    if a.expect_tx is not None and len(tx) != a.expect_tx:
        errs.append("expected %d transmissions, found %d" % (a.expect_tx, len(tx)))
    nid = getattr(a, "node_id", None)
    if nid is not None:                              # TC-01: the node_id decoded from the air must be the provisioned one
        for i, (h, _bs) in enumerate(tx, 1):
            if dec(h)["node"] != nid:
                errs.append("TX %d: node_id %d on the air, provisioned as %d" % (i, dec(h)["node"], nid))
    nseqs, ntx = parse_node(path)
    need = getattr(a, "need_marks", False)
    judge_obs = not nseqs        # A-20: with node marks present the node decides the spacing; the observer error is about +/- 40 ms
    prev_start = None
    for i, (h, bs) in enumerate(tx, 1):
        sp = spacings_ms(bs)
        out.append("TX %d %s copies=%d pdus_per_burst=%s start_to_start_ms=%s" % (
            i, decode(h), len(bs), [len(b) for b in bs], ["%.1f" % s for s in sp]))
        if a.copies and len(bs) != a.copies:
            errs.append("TX %d: %d copies, expected %d" % (i, len(bs), a.copies))
        for k, s in enumerate(sp, 1):
            if judge_obs and abs(s - a.spacing) > a.tol:
                errs.append("TX %d: copy %d starts %.1f ms after copy %d, limit %d +/- %d" % (i, k + 1, s, k, a.spacing, a.tol))
        last_pdu = bs[-1][-1][0]
        quiet = (end - last_pdu) / 1000000.0
        if quiet < a.quiet:
            errs.append("TX %d: capture ends %.1f s after the last PDU, need %.1f s of observed silence" % (i, quiet, a.quiet))
        if prev_start is not None:
            out.append("TX %d first copy %.3f s after the first copy of TX %d" % (i, (bs[0][0][0] - prev_start) / 1e6, i - 1))
        prev_start = bs[0][0][0]
    if need and not nseqs:
        errs.append("no node copy marks in the log (A-20: the spacing is judged from the node's own marks)")
    for i, sq in enumerate(nseqs, 1):
        nsp = [((sq[j][1] - sq[j - 1][1]) & 0xFFFFFFFF) / 1000.0 for j in range(1, len(sq))]
        out.append("NODE seq %d marks=%d start_to_start_ms=%s (node clock, judged)" % (i, len(sq), ["%.1f" % s for s in nsp]))
        if [k for k, _ in sq] != list(range(len(sq))):
            errs.append("node seq %d: copy marks out of order or missing: %s" % (i, [k for k, _ in sq]))
        if a.copies and len(sq) != a.copies:
            errs.append("node seq %d: %d copy marks, expected %d" % (i, len(sq), a.copies))
        for k, s in enumerate(nsp, 1):
            if abs(s - a.spacing) > a.tol:
                errs.append("node seq %d: copy %d starts %.1f ms after copy %d, limit %d +/- %d" % (i, k + 1, s, k, a.spacing, a.tol))
    if nseqs and len(nseqs) != len(tx):
        errs.append("node sent %d sequences, the observer saw %d transmissions" % (len(nseqs), len(tx)))
    for i, (n, (h, _bs)) in enumerate(zip(ntx, tx), 1):        # what the node says it sent against what is on the air
        b = bytes.fromhex(h)
        _seq, typ, ctr, st, boot, flt = n
        want = (0x20 if typ == "event" else 0, 0x10 if boot else 0, 0x04 if flt else 0, 1 if st == 2 else 0, ctr)
        got = (b[4] & 0x20, b[4] & 0x10, b[4] & 0x04, b[7], int.from_bytes(b[9:13], "little"))
        if want != got:
            errs.append("TX %d: the node log says %s ctr=%d state=%d boot=%d fault=%d, the air has %s" % (i, typ, ctr, st, boot, flt, decode(h)))
    cyc, hbn = getattr(a, "cycles", 0), getattr(a, "heartbeats", 0)
    if cyc or hbn:                       # TC-07 / TC-10 run: boot event, N x (remove, wear) events, then heartbeats, counter +1 every time
        want = 1 + 2 * cyc + hbn
        if len(tx) != want:
            errs.append("expected %d transmissions (1 boot event + %d events + %d heartbeats), found %d" % (want, 2 * cyc, hbn, len(tx)))
        d = [dec(h) for h, _bs in tx]
        for i, x in enumerate(d):
            if i and x["node"] != d[0]["node"]:
                errs.append("TX %d: node %d, the first packet was node %d" % (i + 1, x["node"], d[0]["node"]))
            if i and x["ctr"] != d[i - 1]["ctr"] + 1:
                errs.append("TX %d: counter %d after %d, it must be +1" % (i + 1, x["ctr"], d[i - 1]["ctr"]))
            if i == 0:
                if not (x["event"] and x["boot"]):
                    errs.append("TX 1 must be the BOOT=1 event")
            elif i <= 2 * cyc:
                if not x["event"] or x["boot"]:
                    errs.append("TX %d must be an event with BOOT=0" % (i + 1))
                if x["state"] != 1 - d[i - 1]["state"]:
                    errs.append("TX %d: state %d after state %d, it must alternate" % (i + 1, x["state"], d[i - 1]["state"]))
            else:
                if x["event"] or x["boot"]:
                    errs.append("TX %d must be a heartbeat with BOOT=0" % (i + 1))
                if x["state"] != d[2 * cyc]["state"]:
                    errs.append("TX %d: heartbeat state %d, the last confirmed state was %d" % (i + 1, x["state"], d[2 * cyc]["state"]))
        if hbn and len(nseqs) == len(tx):
            hb_t = [nseqs[i][0][1] for i in range(2 * cyc + 1, len(nseqs))]
            gaps = [((hb_t[j] - hb_t[j - 1]) & 0xFFFFFFFF) / 1e6 for j in range(1, len(hb_t))]
            if gaps:
                out.append("HEARTBEAT first copy to first copy, node clock: min %.3f s mean %.3f s max %.3f s over %d gaps (reported, not judged here)" % (min(gaps), sum(gaps) / len(gaps), max(gaps), len(gaps)))
        out.append("RUN %d wear + %d remove events and %d heartbeats checked against the air" % (cyc, cyc, hbn))
    for e in errs:
        out.append("FINDING " + e)
    out.append("RESULT %s" % ("PASS" if not errs else "FAIL (%d)" % len(errs)))
    emit(path, out)
    return 1 if errs else 0


def cmd_calibrate(a):
    path = log_file(a)
    pk, end, lost = parse(path)
    pdus = [p for p in pk if p[1] == a.len and p[4].startswith(a.prefix)]
    tx = transmissions(pdus, int(a.gap * 1000000))
    sp = [s for _, bs in tx for s in spacings_ms(bs)]
    out = ["calibrate %s: transmissions=%d lost=%d nominal spacing %d ms" % (os.path.basename(path), len(tx), lost, a.spacing)]
    if not sp:
        out.append("FINDING no burst spacing in the log (injector did not send, or less than two bursts were seen)")
        emit(path, out)
        return 1
    err = [abs(s - a.spacing) for s in sp]
    per = [len(b) for _, bs in tx for b in bs]
    out.append("bursts=%d spacings=%d mean=%.2f ms min=%.1f max=%.1f stdev=%.2f" % (len(per), len(sp), statistics.mean(sp), min(sp), max(sp), statistics.pstdev(sp)))
    out.append("OBSERVER_ERROR_MS max=%.1f mean=%.2f (spacing seen minus nominal; the injector's own jitter is included)" % (max(err), statistics.mean(err)))
    out.append("pdus_per_burst min=%d max=%d mean=%.1f" % (min(per), max(per), statistics.mean(per)))
    if lost:
        out.append("FINDING observer queue overflow, lost=%d: these numbers are not valid" % lost)
    emit(path, out)
    return 1 if lost else 0


def cmd_result(a):
    logger.append_summary(a.tc, a.result, sprint=SPRINT)
    print("summary row written:", logger.summary_path(SPRINT), a.tc, a.result)
    return 0


def load_plan(path):
    """Lines of '<seconds from the start> <hook command>'. Never a key: a command longer than 40 characters is refused."""
    out = []
    with open(path, encoding="utf-8") as f:
        for ln in f:
            ln = ln.strip()
            if not ln or ln.startswith("#"):
                continue
            t, cmd = ln.split(None, 1)
            if len(cmd) > 40 or "key" in cmd.lower().replace("testkey", ""):
                sys.exit("STOP: plan command not allowed (too long or key-like): %.12s..." % cmd)
            out.append((float(t), cmd))
    return out


def cmd_plan(a):
    """TC-07 / TC-10 run for a node that boots WORN (FOOT build with --adc-boot 2000, adc hook): N x (remove, wear), then heartbeats."""
    lines = ["# plan for observe.py capture --node-plan (seconds from the start of the capture, then the hook line)",
             "%g set grace %d" % (a.first - 2, a.grace)]
    t = a.first
    for _ in range(a.cycles):
        lines.append("%g adc 50" % t); lines.append("%g adc 2000" % (t + a.hold)); t += 2 * a.hold
    if a.hb:
        lines.append("%g set hb %d" % (t + 3, a.hb)); lines.append("%g tx hb" % (t + 4)); t += 4
    with open(a.out, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    hb_end = t + (a.heartbeats - 1) * a.hb if a.hb else t
    print("plan written: %s, %d commands, last command at %g s; run at least --seconds %d (last heartbeat %g s + 2.1 s copies + 10 s silence + 15 s margin)"
          % (a.out, len(lines) - 1, t, int(hb_end + 2.1 + 10 + 15) + 1, hb_end))
    return 0


def node_reader(ser, cap, stop):
    buf = b""
    while not stop.is_set():
        try:
            chunk = ser.read(256)
        except Exception:
            return
        if not chunk:
            continue
        buf += chunk
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            cap.note("NODE " + raw.decode("utf-8", "replace").rstrip("\r"))


def cmd_capture(a):
    path = log_file(a)
    if os.path.exists(path):
        sys.exit("STOP: %s already exists (a log is never appended to). Move it to test_logs/invalid/ or change HEAD." % path)
    header = logger.header_lines(a.tc, a.waived) + ["via=observe.py", "observer_port=%s" % a.port] + a.header
    if a.node:
        header.append("node_port=%s" % a.node)
        if a.node_plan:
            header += ["plan %g %s" % (t, c) for t, c in load_plan(a.node_plan)]
    cap = logger.Capture(a.port, a.baud, path, header)
    print("logging to", path)
    inj = None
    nd = None
    stop = threading.Event()
    try:
        time.sleep(0.5)
        cap.send("RESET"); time.sleep(0.3)
        if a.filter:
            cap.send("FILTER %d" % a.filter); time.sleep(0.3)
        t0 = time.time()
        if a.node:                                          # the node under test: its lines go into the same log as NODE ...
            import serial
            nd = serial.Serial(); nd.port = a.node; nd.baudrate = 115200; nd.timeout = 0.2; nd.dtr = False; nd.rts = False; nd.open()
            threading.Thread(target=node_reader, args=(nd, cap, stop), daemon=True).start()
            plan = [(a.node_after + i * a.node_gap, c) for i, c in enumerate(a.node_send)]
            if a.node_plan:
                plan += load_plan(a.node_plan)
            plan.sort(key=lambda x: x[0])
            for t_cmd, c in plan:
                time.sleep(max(0.0, t_cmd - (time.time() - t0)))
                cap.note("NODETX %s" % c)
                nd.write((c + "\n").encode())
        if a.injector:
            import serial
            inj = serial.Serial(); inj.port = a.injector; inj.baudrate = 115200; inj.timeout = 0.2
            inj.dtr = False; inj.rts = False; inj.open()
            time.sleep(max(0.0, a.adv_after - (time.time() - t0)))
            cap.note("INJECT ADV %s" % a.adv)
            inj.write(("ADV %s\n" % a.adv).encode())
            parts = a.adv.split()
            wait = (int(parts[1]) * int(parts[2]) / 1000.0 if len(parts) >= 3 else 5.0) + 10.0   # all bursts plus 10 s, never longer
            t_cmd = time.time()
            got_ok = False
            while time.time() < t_cmd + wait:
                ln = inj.readline().decode("utf-8", "replace").strip()
                if ln:
                    print("injector:", ln)
                    cap.note("INJECTOR %s" % ln)
                    got_ok = got_ok or ln.startswith("OK")
                if ln.startswith("DONE") or ln.startswith("ERR"):
                    break
                if not got_ok and time.time() > t_cmd + 5.0:
                    print("STOP waiting: the injector gave no OK within 5 s")
                    cap.note("INJECTOR no OK within 5 s")
                    break
        time.sleep(max(0.0, a.seconds - (time.time() - t0)))
        cap.send("TIME"); time.sleep(0.3)
        cap.send("STAT"); time.sleep(0.5)
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        if nd:
            nd.close()
        if inj:
            inj.close()
        cap.close()
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("capture")
    c.add_argument("--port", required=True); c.add_argument("--baud", type=int, default=115200)
    c.add_argument("--tc", required=True); c.add_argument("--seconds", type=float, default=60)
    c.add_argument("--filter", type=int, default=0); c.add_argument("--waived", action="store_true")
    c.add_argument("--header", action="append", default=[]); c.add_argument("--log")
    c.add_argument("--injector"); c.add_argument("--adv"); c.add_argument("--adv-after", type=float, default=3.0)
    c.add_argument("--node", help="COM port of the node under test (hook commands are sent to it)")
    c.add_argument("--node-send", action="append", default=[], help="a hook line for the node; repeat for several")
    c.add_argument("--node-after", type=float, default=5.0, help="seconds after the start before the first node command")
    c.add_argument("--node-gap", type=float, default=1.0, help="seconds between node commands")
    c.add_argument("--node-plan", help="file of timed hook lines made by the plan command")
    for name in ("analyze", "calibrate"):
        p = sub.add_parser(name)
        p.add_argument("--tc", required=True); p.add_argument("--log")
        p.add_argument("--len", type=int, default=21); p.add_argument("--prefix", default="14ffffff")
        p.add_argument("--gap", type=float, default=0.2, help="seconds: PDUs closer than this belong to one burst")
        p.add_argument("--spacing", type=int, default=500)
        if name == "analyze":
            p.add_argument("--copies", type=int, default=5); p.add_argument("--tol", type=int, default=50)
            p.add_argument("--quiet", type=float, default=10.0); p.add_argument("--expect-tx", type=int)
            p.add_argument("--need-marks", action="store_true", help="the log must hold the node's MARK COPY lines (A-20)")
            p.add_argument("--node-id", type=int, help="TC-01: every transmission must carry this node_id")
            p.add_argument("--cycles", type=int, default=0, help="TC-07: boot event then N x (remove, wear); counter +1 and the state alternates")
            p.add_argument("--heartbeats", type=int, default=0, help="TC-10: N heartbeats after the events")
    pl = sub.add_parser("plan")
    pl.add_argument("--cycles", type=int, default=20); pl.add_argument("--hold", type=float, default=6.0)
    pl.add_argument("--first", type=float, default=32.0); pl.add_argument("--grace", type=int, default=2)
    pl.add_argument("--hb", type=int, default=60); pl.add_argument("--heartbeats", type=int, default=20)
    pl.add_argument("--out", required=True)
    r = sub.add_parser("result")
    r.add_argument("--tc", required=True); r.add_argument("--result", required=True, choices=["PASS", "FAIL", "BLOCKED"])
    a = ap.parse_args()
    if a.cmd == "capture" and bool(a.injector) != bool(a.adv):
        ap.error("--injector and --adv go together")
    return {"capture": cmd_capture, "analyze": cmd_analyze, "calibrate": cmd_calibrate, "result": cmd_result, "plan": cmd_plan}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
