#!/usr/bin/env python3
"""observe.py - S4 observer tool (A-14): capture from tools/observer, burst analysis, injector calibration, result row.
All times come from the observer's own esp_timer clock (PKT and TIME lines); the PC time only orders lines (A-01).

  capture   --port COM6 --tc TC-10 --seconds 60 [--filter 21] [--waived] [--header TEXT]
            [--injector COM4 --adv "<hex> <copies> <spacing_ms>" --adv-after 3]   (calibration: the injector sends known bursts)
  analyze   --tc TC-10 [--log PATH] [--copies 5] [--spacing 500] [--tol 50] [--quiet 10] [--expect-tx N]
            [--len 21] [--prefix 14ffffff] [--gap 0.2]       one 'copy' = one burst of PDUs (A-15); exit 0 = PASS, 1 = FAIL
  calibrate --tc OBS-CAL [--log PATH] --spacing 500        measured spacing error of the observer against the injector (no verdict)
  result    --tc TC-10 --result PASS|FAIL|BLOCKED          one row in test_logs/summary_s4.csv

Log: test_logs/S4/<TC>_<sha7>.log. A log is never appended to by a second capture. Never run two programs on one COM port."""
import argparse, os, re, statistics, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import logger

SPRINT = "S4"
PKT = re.compile(r"\| PKT (\d+)\.(\d{6}) (\d+) (\d+) (-?\d+) ([0-9a-f]*)\s*$")
TIME = re.compile(r"\| TIME (\d+)\.(\d{6})\s*$")
LOSS = re.compile(r"\| (?:LOST (\d+)|STAT .*lost=(\d+))")


def log_file(a):
    return a.log or logger.log_path(a.tc, SPRINT)


def parse(path):
    """-> (pdus [(us, len, evt, rssi, hex)], end_us, lost). end_us = last PKT or TIME time of the observer clock."""
    pk, end, lost = [], 0, 0
    with open(path, encoding="utf-8") as f:
        for ln in f:
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
    if not tx:
        errs.append("no matching transmission in the log")
    if lost:
        errs.append("observer queue overflow, lost=%d" % lost)
    wrong = [p for p in pdus if p[2] != 3]
    if wrong:
        errs.append("%d PDUs are not ADV_NONCONN_IND (evt 3), first evt=%d" % (len(wrong), wrong[0][2]))
    if a.expect_tx is not None and len(tx) != a.expect_tx:
        errs.append("expected %d transmissions, found %d" % (a.expect_tx, len(tx)))
    prev_start = None
    for i, (h, bs) in enumerate(tx, 1):
        sp = spacings_ms(bs)
        out.append("TX %d %s copies=%d pdus_per_burst=%s start_to_start_ms=%s" % (
            i, decode(h), len(bs), [len(b) for b in bs], ["%.1f" % s for s in sp]))
        if a.copies and len(bs) != a.copies:
            errs.append("TX %d: %d copies, expected %d" % (i, len(bs), a.copies))
        for k, s in enumerate(sp, 1):
            if abs(s - a.spacing) > a.tol:
                errs.append("TX %d: copy %d starts %.1f ms after copy %d, limit %d +/- %d" % (i, k + 1, s, k, a.spacing, a.tol))
        last_pdu = bs[-1][-1][0]
        quiet = (end - last_pdu) / 1000000.0
        if quiet < a.quiet:
            errs.append("TX %d: capture ends %.1f s after the last PDU, need %.1f s of observed silence" % (i, quiet, a.quiet))
        if prev_start is not None:
            out.append("TX %d first copy %.3f s after the first copy of TX %d" % (i, (bs[0][0][0] - prev_start) / 1e6, i - 1))
        prev_start = bs[0][0][0]
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
    emit(path, out)
    return 0


def cmd_result(a):
    logger.append_summary(a.tc, a.result, sprint=SPRINT)
    print("summary row written:", logger.summary_path(SPRINT), a.tc, a.result)
    return 0


def cmd_capture(a):
    path = log_file(a)
    if os.path.exists(path):
        sys.exit("STOP: %s already exists (a log is never appended to). Move it to test_logs/invalid/ or change HEAD." % path)
    header = logger.header_lines(a.tc, a.waived) + ["via=observe.py", "observer_port=%s" % a.port] + a.header
    cap = logger.Capture(a.port, a.baud, path, header)
    print("logging to", path)
    inj = None
    try:
        time.sleep(0.5)
        if a.filter:
            cap.send("FILTER %d" % a.filter); time.sleep(0.3)
        t0 = time.time()
        if a.injector:
            import serial
            inj = serial.Serial(); inj.port = a.injector; inj.baudrate = 115200; inj.timeout = 0.2
            inj.dtr = False; inj.rts = False; inj.open()
            time.sleep(max(0.0, a.adv_after - (time.time() - t0)))
            cap.note("INJECT ADV %s" % a.adv)
            inj.write(("ADV %s\n" % a.adv).encode())
            t_end = time.time() + 120
            while time.time() < t_end:
                ln = inj.readline().decode("utf-8", "replace").strip()
                if ln:
                    cap.note("INJECTOR %s" % ln)
                if ln.startswith("DONE") or ln.startswith("ERR"):
                    break
        time.sleep(max(0.0, a.seconds - (time.time() - t0)))
        cap.send("TIME"); time.sleep(0.3)
        cap.send("STAT"); time.sleep(0.5)
    except KeyboardInterrupt:
        pass
    finally:
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
    for name in ("analyze", "calibrate"):
        p = sub.add_parser(name)
        p.add_argument("--tc", required=True); p.add_argument("--log")
        p.add_argument("--len", type=int, default=21); p.add_argument("--prefix", default="14ffffff")
        p.add_argument("--gap", type=float, default=0.2, help="seconds: PDUs closer than this belong to one burst")
        p.add_argument("--spacing", type=int, default=500)
        if name == "analyze":
            p.add_argument("--copies", type=int, default=5); p.add_argument("--tol", type=int, default=50)
            p.add_argument("--quiet", type=float, default=10.0); p.add_argument("--expect-tx", type=int)
    r = sub.add_parser("result")
    r.add_argument("--tc", required=True); r.add_argument("--result", required=True, choices=["PASS", "FAIL", "BLOCKED"])
    a = ap.parse_args()
    if a.cmd == "capture" and bool(a.injector) != bool(a.adv):
        ap.error("--injector and --adv go together")
    return {"capture": cmd_capture, "analyze": cmd_analyze, "calibrate": cmd_calibrate, "result": cmd_result}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
