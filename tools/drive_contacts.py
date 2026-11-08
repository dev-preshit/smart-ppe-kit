#!/usr/bin/env python3
"""SC-12 drive_contacts.py - scenario driver for the S2 bench tests (A-09: no relay board, prompt script).
It owns the COM port (do not run logger.py at the same time), writes everything to test_logs/S2/<TC>_<sha7>.log,
checks what it can, and calls analyze_timing.py for the timing results.

Prompted by hand (gloves, helmet):
  contacts --tc TC-02 --kind glove  [--cycles 5]      CONNECT / DISCONNECT, hold 2 s, compare candidate
  contacts --tc TC-04 --kind helmet                   the 4 combinations, only both-closed is WORN
Scripted through the 'adc' hook (FSR node, no hands needed):
  fsr-boot --tc TC-03                                 TC-03 step 2: flash the --adc-boot 800 build, reset, run this: power-on 800 = NOT_WORN
  fsr-seq --tc TC-03                                  step 1: 0,88,89,800,1536,1537,4095,1536,89,88; the verdict also needs step 2 above
  settle  --tc TC-05 [--n 5]                          SETTLE_MS=0 changes, 400 ms pulses, 600 ms holds at SETTLE_MS=500
  grace   --tc TC-06 --scenario a|b|c|d [--trials 10] [--grace 25]
  fault   --tc TC-16                                  adc fail / adc ok, WORN and NOT_WORN starts
Add --port COM3 (default COM3). Add --waived when trial counts are reduced to 5 (header says 'trials=5 waived').
"""
import argparse, os, re, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import logger

STATE = re.compile(r"STATE confirmed=(\d+)/(\d+) candidate=(\d+)/(\d+)")
HERE = os.path.dirname(os.path.abspath(__file__))


def candidate(cap):
    """Ask the node (show hook) for its candidate state. Returns (state, fault) or None."""
    cap.send("show")
    ln = cap.wait_for(lambda l: STATE.search(l) is not None, 2.0)
    if ln is None:
        return None
    m = STATE.search(ln)
    return int(m.group(3)), int(m.group(4))


def confirmed(cap):
    cap.send("show")
    ln = cap.wait_for(lambda l: STATE.search(l) is not None, 2.0)
    if ln is None:
        return None
    m = STATE.search(ln)
    return int(m.group(1)), int(m.group(2))


def wait_boot_confirmed(cap, timeout=40):
    t_end = time.time() + timeout
    while time.time() < t_end:
        c = confirmed(cap)
        if c and c[0] != 0:
            return True
        time.sleep(1)
    return False


def wait_confirm(cap, grace, fault=None):
    def pred(l):
        m = re.search(r"MARK CONFIRM \d+ state=(\d+) fault=(\d+)", l)
        return bool(m) and (fault is None or int(m.group(2)) == fault)
    return cap.wait_for(pred, grace + 6)


def finish(cap, tc, ok, details):
    for d in details:
        cap.note("# RESULT " + d)
    verdict = "PASS" if ok else "FAIL"
    cap.note("# RESULT %s %s" % (tc, verdict))
    cap.close()
    logger.append_summary(tc, verdict)
    print("%s %s" % (tc, verdict))
    return 0 if ok else 1


def analyze(path, kind, extra, tc, summary=True):
    cmd = [sys.executable, os.path.join(HERE, "analyze_timing.py"), path, kind, "--tc", tc] + (["--summary"] if summary else []) + extra
    return subprocess.run(cmd).returncode


def open_cap(a):
    path = logger.log_path(a.tc)
    if os.path.exists(path):                      # never append to an old run's log
        sys.exit("refused: %s already exists. Commit a new sha, or move the old log to test_logs/invalid/ first." % path)
    cap = logger.Capture(a.port, 115200, path, logger.header_lines(a.tc, a.waived))
    time.sleep(0.5)
    return cap, path


def do_contacts(a):
    cap, path = open_cap(a)
    ok, rows = True, []
    if a.kind == "glove":
        for i in range(1, a.cycles + 1):
            for name, want in (("CONNECT", 2), ("DISCONNECT", 1)):
                input("[%d/%d] %s the strap contact, then press Enter " % (i, a.cycles, name))
                cap.note("EXPECT candidate=%d (%s)" % (want, name))
                time.sleep(2.0)
                c = candidate(cap)
                good = c is not None and c[0] == want
                ok = ok and good
                rows.append("cycle %d %s expect %d got %s %s" % (i, name, want, c, "ok" if good else "FAIL"))
    else:
        for crown, strap in ((0, 0), (1, 0), (0, 1), (1, 1)):
            input("Crown switch %s, chin strap %s, then press Enter " % ("CLOSED" if crown else "OPEN", "CLOSED" if strap else "OPEN"))
            want = 2 if (crown and strap) else 1
            cap.note("EXPECT candidate=%d (crown=%d strap=%d)" % (want, crown, strap))
            time.sleep(2.0)
            c = candidate(cap)
            good = c is not None and c[0] == want
            ok = ok and good
            rows.append("crown=%d strap=%d expect %d got %s %s" % (crown, strap, want, c, "ok" if good else "FAIL"))
    return finish(cap, a.tc, ok, rows)


def show_line(cap):
    cap.send("show")
    return cap.wait_for(lambda l: l.startswith("STATE"), 2.0)


def do_fsr_boot(a):
    """TC-03 step 2: node powered on with the injected reading 800 (build with --adc-boot 800)."""
    cap, path = open_cap(a)
    ln = show_line(cap)
    m = STATE.search(ln or "")
    inj = re.search(r"adc_inject=(-?\d+)", ln or "")
    good = bool(m) and bool(inj) and int(inj.group(1)) == 800 and (int(m.group(3)), int(m.group(4))) == (1, 0)
    cap.note("# RESULT TC-03 step2 %s (power-on count 800, candidate %s, adc_inject %s)" % (
        "PASS" if good else "FAIL", (m.group(3) + "/" + m.group(4)) if m else None, inj.group(1) if inj else None))
    cap.close()
    print("TC-03 step 2:", "PASS" if good else "FAIL (was the board flashed with --adc-boot 800 and reset just before?)")
    return 0 if good else 1


def do_fsr_seq(a):
    cap, path = open_cap(a)
    seq = [0, 88, 89, 800, 1536, 1537, 4095, 1536, 89, 88]
    want = [1, 1, 1, 1, 1, 2, 2, 2, 2, 1]
    ok, rows = True, []
    cap.send("set settle 0")
    for n, w in zip(seq, want):
        cap.send("adc %d" % n); time.sleep(0.3)
        c = candidate(cap)
        good = c is not None and c[0] == w and c[1] == 0
        ok = ok and good
        rows.append("count %d expect state %d got %s %s" % (n, w, c, "ok" if good else "FAIL"))
    with open(path, encoding="utf-8") as f:
        step2 = "RESULT TC-03 step2 PASS" in f.read()
    rows.append("step 2 (power-on 800 gives NOT_WORN) %s" % ("PASS, found in this log" if step2 else
                "MISSING: flash with --adc-boot 800, reset, run 'drive_contacts.py fsr-boot --tc TC-03', then repeat this step"))
    ok = ok and step2
    return finish(cap, a.tc, ok, rows)


def do_settle(a):
    cap, path = open_cap(a)
    cap.send("set settle 0"); cap.send("adc 50"); time.sleep(1.5)
    cap.note("# STEP s0 begin")
    for _ in range(a.n):                         # step 1: SETTLE_MS=0 changes
        cap.send("adc 2000"); time.sleep(0.6); cap.send("adc 50"); time.sleep(0.6)
    cap.note("# STEP s0 end")
    cap.send("set settle 500"); cap.send("adc 50"); time.sleep(1.5)
    cap.note("# STEP pulse begin")
    for _ in range(a.n):                         # step 2: 400 ms pulses
        cap.send("adc 2000"); time.sleep(0.4); cap.send("adc 50"); time.sleep(1.2)
    cap.note("# STEP pulse end")
    cap.note("# STEP hold begin")
    for _ in range(a.n):                         # step 3: 600 ms holds
        cap.send("adc 2000"); time.sleep(0.6); cap.send("adc 50"); time.sleep(1.5)
    cap.note("# STEP hold end")
    cap.close()
    rc = analyze(path, "settle", ["--mode", "s0", "--step", "s0"], a.tc, summary=False)
    rc |= analyze(path, "settle", ["--mode", "pulse", "--step", "pulse"], a.tc, summary=False)
    rc |= analyze(path, "settle", ["--mode", "hold", "--step", "hold"], a.tc, summary=False)
    logger.append_summary(a.tc, "PASS" if rc == 0 else "FAIL")          # one row per test
    print("%s %s" % (a.tc, "PASS" if rc == 0 else "FAIL"))
    return rc


def ensure_not_worn(cap, grace):
    """Precondition for the grace scenarios: the node must be confirmed NOT_WORN (1/0) before trial 1.
    Returns None when confirmed, otherwise the reason."""
    cap.send("adc 50")
    if not wait_boot_confirmed(cap, 40 + grace):
        return "boot grace never completed"
    last, t_end = None, time.time() + grace + 10
    while time.time() < t_end:
        last = confirmed(cap)
        if last == (1, 0):
            return None
        time.sleep(1)
    return "node not confirmed NOT_WORN (last confirmed=%s) within %d s of adc 50" % (last, grace + 10)


def do_grace(a):
    cap, path = open_cap(a)
    cap.send("set grace %d" % a.grace); cap.send("set settle 0")
    why = ensure_not_worn(cap, a.grace)
    if why:
        cap.note("# RESULT precondition failed: " + why); cap.close()
        invalid = os.path.normpath(os.path.join(os.path.dirname(path), "..", "invalid"))
        os.makedirs(invalid, exist_ok=True)
        os.replace(path, os.path.join(invalid, os.path.basename(path)))
        print("FAIL precondition: %s. Log moved to %s, no summary row." % (why, invalid)); return 1
    hi, lo = 2000, 50
    if a.scenario in ("a", "d"):
        for i in range(a.trials):
            cap.send("adc %d" % (hi if i % 2 == 0 else lo))
            if wait_confirm(cap, a.grace) is None:
                cap.note("# RESULT trial %d: no CONFIRM within grace+6 s" % i)
    elif a.scenario == "b":
        cap.send("adc %d" % hi); time.sleep(10); cap.send("adc %d" % lo); time.sleep(a.grace + 6)
    elif a.scenario == "c":
        cap.send("adc %d" % hi); time.sleep(10); cap.send("adc %d" % lo); time.sleep(2); cap.send("adc %d" % hi)
        if wait_confirm(cap, a.grace + 14) is None:
            cap.note("# RESULT scenario c: no CONFIRM")
    cap.close()
    return analyze(path, "grace", ["--scenario", a.scenario, "--grace", str(a.grace)], a.tc)


def do_fault(a):
    cap, path = open_cap(a)
    g = a.grace
    cap.send("set grace %d" % g); cap.send("set settle 0"); cap.send("adc ok"); cap.send("adc 2000")
    if not wait_boot_confirmed(cap, 40 + g):
        cap.note("# RESULT boot grace never completed"); cap.close()
        print("FAIL: node never completed the boot grace, see", path); return 1
    for start, label in ((2000, "WORN"), (50, "NOT_WORN")):
        cap.send("adc %d" % start)
        wait_confirm(cap, g, fault=0)
        cap.note("# STEP node confirmed %s, adc fail" % label)
        cap.send("adc fail")
        if wait_confirm(cap, g, fault=1) is None:
            cap.note("# RESULT %s: no CONFIRM with fault=1" % label)
        cap.send("adc ok")
        if wait_confirm(cap, g, fault=0) is None:
            cap.note("# RESULT %s: no CONFIRM with fault=0" % label)
    cap.close()
    return analyze(path, "fault", ["--grace", str(g)], a.tc)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", choices=["contacts", "fsr-boot", "fsr-seq", "settle", "grace", "fault"])
    ap.add_argument("--tc", required=True); ap.add_argument("--port", default="COM3")
    ap.add_argument("--kind", choices=["glove", "helmet"], default="glove")
    ap.add_argument("--cycles", type=int, default=5); ap.add_argument("--n", type=int, default=5)
    ap.add_argument("--scenario", choices=["a", "b", "c", "d"], default="a")
    ap.add_argument("--trials", type=int, default=10); ap.add_argument("--grace", type=int, default=25)
    ap.add_argument("--waived", action="store_true")
    a = ap.parse_args()
    return {"contacts": do_contacts, "fsr-boot": do_fsr_boot, "fsr-seq": do_fsr_seq, "settle": do_settle, "grace": do_grace, "fault": do_fault}[a.mode](a)


if __name__ == "__main__":
    sys.exit(main())
