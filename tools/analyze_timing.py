#!/usr/bin/env python3
"""SC-07 analyze_timing.py - timing checks from a serial log (A-01). Uses ONLY node micros() differences from MARK lines.

Log line:  <PC time> | MARK <NAME> <micros> [state=n fault=n]      (PC time only orders lines)
  grace   TC-06: GRACE_START -> CONFIRM, expected GRACE_S +- 0.5 s (also CONFIRM_BOOT). --scenario a|b|c|d
  settle  TC-05: --mode s0 (RAW -> CAND <= 50 ms) | pulse (400 ms pulse gives no CAND) | hold (600 ms change gives CAND)
  fault   TC-16: first RAW with fault=1 -> CONFIRM with fault=1, and first RAW back to fault=0 -> CONFIRM fault=0, 25 +- 0.5 s
The result is appended to the same log ('# ANALYSIS ...') and, with --summary, one row goes to test_logs/summary.csv.
Exit code 0 = PASS, 1 = FAIL.
"""
import argparse, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import logger

MARK = re.compile(r"MARK\s+(\w+)\s+(\d+)(?:\s+state=(\d+)\s+fault=(\d+))?")
TOL = 0.5


def parse(path, step=None):
    """MARK lines of the log. With step=NAME only the lines between '# STEP NAME begin' and '# STEP NAME end'."""
    marks, inside = [], step is None
    with open(path, encoding="utf-8") as f:
        for line in f:
            body = line.split("|", 1)[-1].strip()
            if step is not None and body.startswith("# STEP %s " % step):
                inside = body.endswith("begin"); continue
            if not inside:
                continue
            m = MARK.search(body)
            if m:
                marks.append((m.group(1), int(m.group(2)), None if m.group(3) is None else int(m.group(3)),
                              None if m.group(4) is None else int(m.group(4))))
    return marks


def dt_s(t0, t1):                                 # micros() wraps at 2^32
    return ((t1 - t0) & 0xFFFFFFFF) / 1e6


def check_grace(marks, grace_s, scenario, include_boot=False):
    out, ok = [], True
    kinds = ("CONFIRM", "CONFIRM_BOOT") if include_boot else ("CONFIRM",)
    confirms = [i for i, m in enumerate(marks) if m[0] in kinds]
    if scenario == "b":                           # reverted at 10 s: a GRACE_CANCEL and no CONFIRM after the change
        cancels = sum(1 for m in marks if m[0] == "GRACE_CANCEL")
        out.append("b: GRACE_CANCEL=%d CONFIRM=%d (expect >=1 and 0)" % (cancels, len(confirms)))
        return cancels >= 1 and len(confirms) == 0, out
    if not confirms:
        return False, ["no CONFIRM mark found"]
    for ci in confirms:
        starts = [j for j in range(ci) if marks[j][0] == "GRACE_START"]
        if not starts:
            ok = False; out.append("CONFIRM without GRACE_START"); continue
        last = starts[-1]
        if scenario == "c":                       # change, revert at 10 s, change again at 12 s: event at 37 s from the first change
            cancels = [j for j in range(last) if marks[j][0] == "GRACE_CANCEL"]
            before = [j for j in starts if cancels and j < cancels[-1]]
            if not cancels or not before:
                ok = False; out.append("c: no revert (GRACE_CANCEL) between two GRACE_START marks"); continue
            d = dt_s(marks[before[-1]][1], marks[ci][1])
            good = abs(d - 37.0) <= TOL
            out.append("c: first change -> CONFIRM %.3f s (expect 37 +- %.1f) %s" % (d, TOL, "ok" if good else "FAIL"))
        else:
            d = dt_s(marks[last][1], marks[ci][1])
            good = abs(d - grace_s) <= TOL
            out.append("%s: GRACE_START -> %s %.3f s (expect %g +- %.1f) %s" % (scenario, marks[ci][0], d, grace_s, TOL, "ok" if good else "FAIL"))
        ok = ok and good
    return ok, out


def check_settle(marks, mode):
    out, ok = [], True
    raws = [m for m in marks if m[0] == "RAW"]
    if mode == "s0":
        pairs = 0
        for i, m in enumerate(marks):
            if m[0] == "RAW":
                nxt = next((x for x in marks[i + 1:] if x[0] in ("CAND", "RAW")), None)
                if nxt and nxt[0] == "CAND":
                    d = dt_s(m[1], nxt[1]) * 1000; pairs += 1
                    good = d <= 50; ok = ok and good
                    out.append("s0: RAW -> CAND %.1f ms (limit 50) %s" % (d, "ok" if good else "FAIL"))
        if pairs == 0:
            ok = False; out.append("no RAW -> CAND pair found")
    elif mode == "pulse":
        # a pulse = RAW away from baseline, then RAW back within < settle; no CAND between them
        n = 0
        for i in range(len(marks) - 1):
            if marks[i][0] == "RAW" and marks[i][2] == 2:        # a pulse leaves the NOT_WORN baseline
                nxt = next((x for x in marks[i + 1:] if x[0] in ("CAND", "RAW")), None)
                if nxt and nxt[0] == "RAW":
                    w = dt_s(marks[i][1], nxt[1]) * 1000; n += 1
                    good = w < 500; ok = ok and good
                    out.append("pulse: width %.0f ms, no CAND in between %s" % (w, "ok" if good else "FAIL (pulse too long to test)"))
        cands = sum(1 for m in marks if m[0] == "CAND")
        out.append("pulses=%d CAND marks=%d (expect 0)" % (n, cands)); ok = ok and n > 0 and cands == 0
    elif mode == "hold":
        n = 0
        for i, m in enumerate(marks):
            if m[0] == "RAW":
                nxt = next((x for x in marks[i + 1:] if x[0] in ("CAND", "RAW")), None)
                if nxt and nxt[0] == "CAND":
                    d = dt_s(m[1], nxt[1]) * 1000; n += 1
                    good = 500 <= d <= 600; ok = ok and good
                    out.append("hold: RAW -> CAND %.0f ms (500..600) %s" % (d, "ok" if good else "FAIL"))
        if n == 0:
            ok = False; out.append("no CAND found")
    return ok, out


def check_fault(marks, grace_s):
    out, ok, n = [], True, 0
    for i, m in enumerate(marks):
        if m[0] == "RAW" and m[3] in (0, 1):
            prev_fault = next((x[3] for x in reversed(marks[:i]) if x[0] in ("RAW", "COLD_START")), 0)
            if m[3] == prev_fault:
                continue
            want_fault = m[3]
            conf = next((x for x in marks[i + 1:] if x[0] == "CONFIRM" and x[3] == want_fault), None)
            if conf is None:
                ok = False; out.append("fault=%d at %d: no CONFIRM with that fault" % (want_fault, m[1])); continue
            d = dt_s(m[1], conf[1]); n += 1
            good = abs(d - grace_s) <= TOL; ok = ok and good
            out.append("first sample fault=%d -> CONFIRM fault=%d %.3f s (expect %g +- %.1f) %s" % (want_fault, want_fault, d, grace_s, TOL, "ok" if good else "FAIL"))
    return ok and n > 0, out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log"); ap.add_argument("kind", choices=["grace", "settle", "fault"])
    ap.add_argument("--scenario", default="a", choices=["a", "b", "c", "d"])
    ap.add_argument("--mode", default="s0", choices=["s0", "pulse", "hold"])
    ap.add_argument("--grace", type=float, default=25.0)
    ap.add_argument("--include-boot", action="store_true", help="also check the CONFIRM_BOOT event (FR-08)")
    ap.add_argument("--step", help="only the lines between '# STEP <name> begin' and '# STEP <name> end'")
    ap.add_argument("--tc"); ap.add_argument("--summary", action="store_true")
    a = ap.parse_args()
    marks = parse(a.log, a.step)
    if a.kind == "grace":
        ok, out = check_grace(marks, a.grace, a.scenario, a.include_boot)
    elif a.kind == "settle":
        ok, out = check_settle(marks, a.mode)
    else:
        ok, out = check_fault(marks, a.grace)
    verdict = "PASS" if ok else "FAIL"
    with open(a.log, "a", encoding="utf-8", newline="\n") as f:
        for ln in out:
            f.write("# ANALYSIS %s\n" % ln)
        f.write("# ANALYSIS RESULT %s\n" % verdict)
    for ln in out:
        print(ln)
    print("RESULT", verdict)
    if a.summary:
        tc = a.tc or os.path.basename(a.log).split("_")[0]
        logger.append_summary(tc, verdict)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
