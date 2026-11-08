#!/usr/bin/env python3
"""SC-06 logger.py - timestamped serial capture to test_logs/S2/<TC>_<sha7>.log (and a helper for summary_s2.csv).

Every device line is stored as   <PC ISO time> | <device line>.  The PC time only orders lines; all timing
checks use the node's own micros() inside MARK lines (A-01). Do not run two programs on one COM port: the
contact/scenario driver (drive_contacts.py) imports this module and does the capture itself.

  python tools/logger.py --port COM3 --tc TC-03 --seconds 60 [--waived] [--send show]
"""
import argparse, csv, datetime, os, subprocess, sys, threading, time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SUMMARY_HEADER = ["TC", "sprint", "date", "sha", "result"]


def git_sha7():
    try:
        out = subprocess.run(["git", "rev-parse", "--short=7", "HEAD"], cwd=REPO, capture_output=True, text=True)
        return out.stdout.strip() or "nogit00"
    except OSError:
        return "nogit00"


def log_path(tc, sprint="S2", sha=None, root=None):
    root = root or os.path.join(REPO, "test_logs")
    return os.path.join(root, sprint, "%s_%s.log" % (tc, sha or git_sha7()))


def now_iso():
    return datetime.datetime.now().isoformat(timespec="milliseconds")


class Capture:
    """Background serial reader that appends '<time> | <line>' to a log file and keeps the lines in memory."""

    def __init__(self, port, baud, path, header=None, serial_factory=None):
        if serial_factory:
            self.ser = serial_factory(port, baud, timeout=0.1)
        else:                                            # DTR/RTS low before open: do not reset the C3 on connect
            import serial  # pyserial
            self.ser = serial.Serial(); self.ser.port = port; self.ser.baudrate = baud; self.ser.timeout = 0.1
            self.ser.dtr = False; self.ser.rts = False; self.ser.open()
        os.makedirs(os.path.dirname(path), exist_ok=True)
        self.f = open(path, "a", encoding="utf-8", newline="\n")
        self.lines, self._stop, self._lock, self._sent_at = [], False, threading.Lock(), 0
        for h in (header or []):
            self.f.write("# %s\n" % h)
        self.f.flush()
        self.t = threading.Thread(target=self._run, daemon=True)
        self.t.start()

    def _run(self):
        buf = b""
        while not self._stop:
            try:
                chunk = self.ser.read(256)
            except Exception as e:                       # port dropped
                self.note("serial error: %s" % e)
                return
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                line = raw.decode("utf-8", "replace").rstrip("\r")
                with self._lock:
                    self.lines.append(line)
                    self.f.write("%s | %s\n" % (now_iso(), line)); self.f.flush()

    def note(self, text):
        """A line written by the PC (EXPECT, STEP, RESULT ...), stored in the same log."""
        with self._lock:
            self.lines.append(text)
            self.f.write("%s | %s\n" % (now_iso(), text)); self.f.flush()

    def send(self, text):
        with self._lock:
            self._sent_at = len(self.lines)               # wait_for() only looks at lines after the last send
        self.note("TX %s" % text)
        self.ser.write((text + "\n").encode())

    def wait_for(self, predicate, timeout_s):
        """Wait until a device line after the last send() satisfies predicate(line). Returns the line or None."""
        with self._lock:
            seen = self._sent_at
        t_end = time.time() + timeout_s
        while time.time() < t_end:
            with self._lock:
                new, seen = self.lines[seen:], len(self.lines)
            for ln in new:
                if predicate(ln):
                    return ln
            time.sleep(0.02)
        return None

    def close(self):
        self._stop = True; self.t.join(timeout=1); self.f.close(); self.ser.close()


def header_lines(tc, waived):
    h = ["tc=%s" % tc, "sha=%s" % git_sha7(), "date=%s" % now_iso(), "tool=logger.py"]
    if waived:
        h.append("trials=5 waived")          # A-10: owner's waiver, noted in docs/CHANGES.md
    return h


def summary_path(sprint="S2", root=None):
    """One summary file per story: test_logs/summary_s2.csv for S2, summary_s3.csv for S3 (owner rule, 2026-10-20)."""
    return os.path.join(root or os.path.join(REPO, "test_logs"), "summary_%s.csv" % sprint.lower())


def append_summary(tc, result, sprint="S2", sha=None, root=None):
    """One row per test in the story's own summary file, header TC,sprint,date,sha,result (A-11)."""
    path = summary_path(sprint, root)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    new = not os.path.exists(path) or os.path.getsize(path) == 0
    with open(path, "a", newline="", encoding="utf-8") as f:
        w = csv.writer(f, lineterminator="\n")
        if new:
            w.writerow(SUMMARY_HEADER)
        w.writerow([tc, sprint, datetime.date.today().isoformat(), sha or git_sha7(), result])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True); ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--tc", required=True); ap.add_argument("--seconds", type=float, default=60)
    ap.add_argument("--waived", action="store_true", help="write 'trials=5 waived' in the header")
    ap.add_argument("--send", action="append", default=["show"], help="command sent after opening (repeatable)")
    a = ap.parse_args()
    path = log_path(a.tc)
    cap = Capture(a.port, a.baud, path, header_lines(a.tc, a.waived))
    print("logging to", path)
    try:
        time.sleep(0.5)
        for c in a.send:
            cap.send(c); time.sleep(0.2)
        time.sleep(a.seconds)
    except KeyboardInterrupt:
        pass
    finally:
        cap.close()


if __name__ == "__main__":
    sys.exit(main())
