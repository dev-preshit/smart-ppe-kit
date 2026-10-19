#!/usr/bin/env python3
"""S3 evidence helper: serial capture, result recording and pass-rule analysis (written because tools/logger.py is S2's).

Evidence rule (same as S2): one serial log per test at  test_logs/S3/<TC>_<sha7>.log  plus ONE row in
test_logs/summary.csv (columns TC,sprint,date,sha,result; append only, on a merge conflict keep both lines).
Log line format: ISO-8601 time, device, message.  A repeated run on the same commit appends under a "# RUN" header.

  python tools/s3_capture.py capture --port COM5 --tc TC-21 --seconds 120 --send-after 5:"stall 1" --send-after 40:dump
  python tools/s3_capture.py path --tc TC-22                 # prints the log path (use it as inject.py --log)
  python tools/s3_capture.py analyze --tc TC-20 --expected 120 --node 5
  python tools/s3_capture.py analyze --tc TC-20 --expected 120 --recheck     # with Wi-Fi connected
  python tools/s3_capture.py analyze --tc TC-72
  python tools/s3_capture.py result --tc TC-20 --result PASS --note "119 of 120"

Never run this and another program (IDE monitor, tools/inject.py --via hub) on the same COM port.
"""
import argparse
import csv
import datetime
import os
import re
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SPRINT = "S3"
TC_RE = re.compile(r"^TC-\d{2}$")


def now_iso(sec=False):
    return datetime.datetime.now().astimezone().isoformat(timespec="seconds" if sec else "milliseconds")


def sha7(root=ROOT):
    r = subprocess.run(["git", "rev-parse", "--short=7", "HEAD"], cwd=root, capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else "nogit00"


def dirty(root=ROOT):
    r = subprocess.run(["git", "status", "--porcelain", "--untracked-files=no"], cwd=root, capture_output=True, text=True)
    return bool(r.stdout.strip())


def log_path(root, tc, sha):
    if not TC_RE.match(tc):
        raise ValueError("test case must look like TC-20")
    return os.path.join(root, "test_logs", SPRINT, "%s_%s.log" % (tc, sha))


def append_line(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "a", encoding="utf-8", newline="\n") as f:
        f.write(text + "\n")


def append_summary(root, tc, result, sha, when=None):
    if result not in ("PASS", "FAIL", "BLOCKED"):
        raise ValueError("result must be PASS, FAIL or BLOCKED")
    path = os.path.join(root, "test_logs", "summary.csv")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    new = not os.path.exists(path)
    with open(path, "a", newline="", encoding="utf-8") as f:
        w = csv.writer(f, lineterminator="\n")
        if new:
            w.writerow(["TC", "sprint", "date", "sha", "result"])
        w.writerow([tc, SPRINT, when or now_iso(sec=True), sha, result])
    return path


# ---------------------------------------------------------------- analysis (pass rules from the test plan)
def last_dump_value(text, pattern):
    m = None
    for m in re.finditer(pattern, text):
        pass
    return m


def last_run_block(text):
    i = text.rfind("# RUN")
    return text[i:] if i >= 0 else text


def node_dumps(text, node):
    pat = (r"DUMP node=%d state=\w+ last_counter=(\d+) last_seen_ms=\d+ accepted=(\d+) dup=(\d+) replay=(\d+) "
           r"gap=(\d+) hmac_fail=(\d+)" % node)
    return [tuple(int(x) for x in m.groups()) for m in re.finditer(pat, text)]


def analyze_tc20(text, node, expected, recheck=False):
    """TC-20: beacon packets accepted >= 99 %; manager rule: more than 1 miss in 120 is a FAIL to investigate.
    Uses the latest "# RUN" block: delta between its first and last dump (take the first dump after the first beacon
    packet), or the single dump if the hub was reset right before the run.
    recheck=True (Wi-Fi + BLE recheck 1) also requires gap_count growth == misses ("gaps minus bad stays 0")."""
    d = node_dumps(last_run_block(text), node)
    if not d:
        return "BLOCKED", "no DUMP line for node %d in the latest run (send 'dump' at the start and at the end)" % node
    first, last = d[0], d[-1]
    if len(d) >= 2:
        acc, gap, basis = last[1] - first[1], last[4] - first[4], "delta of %d dumps" % len(d)
    else:
        acc, gap, basis = last[1], last[4], "single dump, hub assumed reset before the run"
    misses = max(expected - acc, 0)
    pct = 100.0 * acc / expected
    ok = pct >= 99.0 and misses <= 1
    msg = "accepted %d of %d expected = %.2f %%, misses %d, gap_count growth %d (%s)" % (acc, expected, pct, misses, gap, basis)
    if recheck:
        ok = ok and gap == misses
        msg += ", gaps minus misses = %d" % (gap - misses)
    return ("PASS" if ok else "FAIL"), msg


def analyze_tc72(text, min_count=1000):
    """TC-72: max processing time <= 10 ms over >= 1000 packets, no advertisement lost to a full queue."""
    p = last_dump_value(text, r"DUMP proc count=(\d+) last_us=(\d+) max_us=(\d+) avg_us=(\d+) over_10ms=(\d+)")
    q = last_dump_value(text, r"DUMP queue fill=\d+ depth=\d+ max_fill=(\d+) queue_full=(\d+)")
    w = last_dump_value(text, r"DUMP persist pending=\d+ writes=(\d+) write_fails=(\d+) dropped=(\d+) last_us=(\d+) max_us=(\d+) avg_us=(\d+)")
    if not p or not q:
        return "BLOCKED", "no DUMP proc/queue lines in the log"
    count, last, mx, avg, over = (int(x) for x in p.groups())
    max_fill, qfull = (int(x) for x in q.groups())
    ok = count >= min_count and mx <= 10000 and over == 0 and qfull == 0
    msg = "processed %d, max %d us, avg %d us, over_10ms %d, queue max_fill %d, queue_full %d" % (count, mx, avg, over, max_fill, qfull)
    if w:
        msg += "; NVS writes %s, last %s us, max %s us, avg %s us, fails %s" % (w.group(1), w.group(4), w.group(5), w.group(6), w.group(2))
    if count < min_count:
        msg += " (need at least %d packets)" % min_count
    return ("PASS" if ok else "FAIL"), msg


# ---------------------------------------------------------------- commands
def cmd_capture(a):
    import serial
    path = a.out or log_path(ROOT, a.tc, sha7())
    append_line(path, "# RUN %s tc=%s device=%s port=%s label=%s%s" % (now_iso(), a.tc, a.device, a.port, a.label,
                                                                    "  (working tree has uncommitted changes)" if dirty() else ""))
    sends = [(0.0, s) for s in (a.send or [])]
    for item in a.send_after or []:
        sec, _, cmd = item.partition(":")
        sends.append((float(sec), cmd))
    sends.sort(key=lambda x: x[0])
    s = serial.Serial(a.port, a.baud, timeout=0.2)
    t0 = time.time()
    print("capturing %s -> %s (Ctrl-C to stop)" % (a.port, path))
    buf = b""
    try:
        while a.seconds is None or time.time() - t0 < a.seconds:
            while sends and time.time() - t0 >= sends[0][0]:
                cmd = sends.pop(0)[1]
                s.write((cmd + "\n").encode())
                append_line(path, "%s,pc,> %s" % (now_iso(), cmd))
            buf += s.read(512)
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode(errors="replace").rstrip("\r")
                rec = "%s,%s,%s" % (now_iso(), a.device, text)
                append_line(path, rec)
                print(rec)
    except KeyboardInterrupt:
        pass
    append_line(path, "# END %s" % now_iso())
    return 0


def cmd_path(a):
    print(log_path(ROOT, a.tc, sha7()))
    return 0


def cmd_analyze(a):
    path = a.log or log_path(ROOT, a.tc, sha7())
    text = open(path, encoding="utf-8", errors="replace").read()
    if a.tc == "TC-20":
        verdict, msg = analyze_tc20(text, a.node, a.expected, a.recheck)
    elif a.tc == "TC-72":
        verdict, msg = analyze_tc72(text, a.min_count)
    else:
        print("analyze supports TC-20 and TC-72; read the other tests from the DUMP lines (see firmware/hub/README.md)")
        return 2
    print("%s %s: %s" % (verdict, a.tc, msg))
    return {"PASS": 0, "FAIL": 1}.get(verdict, 3)


def cmd_result(a):
    sha = sha7()
    path = log_path(ROOT, a.tc, sha)
    append_line(path, "# RESULT,%s,%s,%s" % (a.result, now_iso(sec=True), a.note or ""))
    csv_path = append_summary(ROOT, a.tc, a.result, sha)
    print("recorded %s %s in %s and %s" % (a.tc, a.result, path, csv_path))
    if dirty():
        print("WARNING: the working tree has uncommitted changes; commit before the final run so the sha matches the code")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("capture")
    p.add_argument("--port", required=True)
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--tc", required=True)
    p.add_argument("--device", default="hub")
    p.add_argument("--seconds", type=float)
    p.add_argument("--send", action="append", help="command sent at the start (repeatable)")
    p.add_argument("--send-after", action="append", help="SECONDS:command, sent after that many seconds (repeatable)")
    p.add_argument("--out", help="override the log path")
    p.add_argument("--label", default="", help="free text in the RUN header, e.g. wifi")
    p.set_defaults(fn=cmd_capture)
    p = sub.add_parser("path")
    p.add_argument("--tc", required=True)
    p.set_defaults(fn=cmd_path)
    p = sub.add_parser("analyze")
    p.add_argument("--tc", required=True)
    p.add_argument("--log")
    p.add_argument("--expected", type=int, default=120)
    p.add_argument("--node", type=int, default=5)
    p.add_argument("--min-count", type=int, default=1000)
    p.add_argument("--recheck", action="store_true", help="TC-20 with Wi-Fi connected: also require gaps == misses")
    p.set_defaults(fn=cmd_analyze)
    p = sub.add_parser("result")
    p.add_argument("--tc", required=True)
    p.add_argument("--result", required=True, choices=["PASS", "FAIL", "BLOCKED"])
    p.add_argument("--note")
    p.set_defaults(fn=cmd_result)
    a = ap.parse_args(argv)
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
