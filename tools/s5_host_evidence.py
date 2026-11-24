#!/usr/bin/env python3
"""S5 evidence for the host-only test cases (TC-26, TC-28; decision D6 / A-35): runs `make test` in test/host and keeps
the output lines of ONE test case in test_logs/S5/<TC>_<sha7>.log, plus one summary_s5.csv row after a real PASS.

  python tools/s5_host_evidence.py run --tc TC-26                 # evidence log at HEAD (clean tree required)
  python tools/s5_host_evidence.py run --tc TC-26 --rehearsal     # scratch log, can never be recorded
  python tools/s5_host_evidence.py analyze --log test_logs/S5/TC-26_<sha7>.log
  python tools/s5_host_evidence.py result --tc TC-26 --result PASS --note "7 tests"

PASS needs: make exits 0, the whole suite reports failed=0, and at least the expected number of tests carry the tag
TC-nn and every one of them says PASS. The driver refuses to append to an existing log. Does not edit tools/logger.py.
"""
import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import logger  # noqa: E402

SPRINT = "S5"
MIN_TESTS = {"TC-26": 7, "TC-28": 11}      # what the committed test files carry today; a lower count means tests vanished
RE_VERDICT = re.compile(r"(?:^|\| )VERDICT (PASS|FAIL|BLOCKED)\b")


def tagged_lines(output, tc):
    return [l for l in output.splitlines() if l.startswith("TEST\t") and l.split("\t")[1:2] == [tc]]


def analyze_output(output, tc, make_rc=0):
    """(verdict, message) from the text of `make test` (or from a saved log: the lines are the same)."""
    mine = tagged_lines(output, tc)
    summ = re.search(r"^SUMMARY\ttests=(\d+)\tfailed=(\d+)\tchecks=(\d+)", output, re.M)
    if not summ:
        return "BLOCKED", "no SUMMARY line: make did not run the tests (build error?)"
    tests, failed, checks = (int(x) for x in summ.groups())
    bad = [l for l in mine if l.split("\t")[3:4] != ["PASS"]]
    need = MIN_TESTS.get(tc, 1)
    msg = "%s: %d tests tagged, %d not PASS; whole suite %d tests, failed=%d, checks=%d" % (tc, len(mine), len(bad), tests, failed, checks)
    if make_rc != 0 or failed != 0:
        return "FAIL", msg + " (the suite has failing tests, make exit %d)" % make_rc
    if bad:
        return "FAIL", msg
    if len(mine) < need:
        return "FAIL", msg + "; expected at least %d" % need
    return "PASS", msg


def make_test():
    r = subprocess.run(["make", "test"], cwd=os.path.join(logger.REPO, "test", "host"), capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    return r.returncode, r.stdout + ("\n" + r.stderr if r.stderr else "")


def cmd_run(a):
    path = logger.log_path(a.tc, sprint="scratch" if a.rehearsal else SPRINT)
    if os.path.exists(path):
        print("ERROR: %s exists: a log is one run, never appended" % path)
        return 2
    if not a.rehearsal:
        dirty = subprocess.run(["git", "status", "--porcelain", "--untracked-files=no"], cwd=logger.REPO, capture_output=True, text=True).stdout.strip()
        if dirty:
            print("ERROR: tracked files are modified: commit first, the HEAD sha names the log")
            return 2
    sha0 = logger.git_sha7()
    rc, out = make_test()
    if not a.rehearsal and logger.git_sha7() != sha0:
        print("ERROR: HEAD changed during the run")
        return 2
    v, msg = analyze_output(out, a.tc, rc)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("# tc=%s\n# sha=%s\n# date=%s\n# tool=s5_host_evidence.py\n# mode=%s\n# command=make test (test/host), make exit=%d\n"
                % (a.tc, sha0, logger.now_iso(), "rehearsal" if a.rehearsal else "evidence", rc))
        f.write("# equipment=host only (no hardware); evidence for the host part (level U), decision D6/A-35\n")
        for l in tagged_lines(out, a.tc):
            f.write(l + "\n")
        s = re.search(r"^SUMMARY\t.*$", out, re.M)
        f.write((s.group(0) if s else "SUMMARY missing") + "\n")
        rel = [l for l in out.splitlines() if l.startswith("release_check")]
        for l in rel:
            f.write("# " + l + "\n")
        f.write("VERDICT %s %s\n" % (v, msg))
    print("log:", path)
    print("%s %s" % (v, msg))
    if a.rehearsal:
        print("(rehearsal: the result cannot be recorded)")
    return {"PASS": 0, "FAIL": 1}.get(v, 3)


def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


def cmd_analyze(a):
    text = read(a.log)
    m = re.search(r"^# tc=(TC-\d+)", text, re.M)
    if not m:
        print("BLOCKED: no '# tc=' header")
        return 3
    v, msg = analyze_output(text, m.group(1), 0)
    print("%s %s" % (v, msg))
    return {"PASS": 0, "FAIL": 1}.get(v, 3)


def cmd_result(a, root=None):
    sha = logger.git_sha7()
    path = logger.log_path(a.tc, sprint=SPRINT, sha=sha, root=os.path.join(root, "test_logs") if root else None)
    if not os.path.exists(path):
        print("ERROR: no evidence log for %s at HEAD %s" % (a.tc, sha))
        sys.exit(2)
    text = read(path)
    vs = [m for m in (RE_VERDICT.search(l) for l in text.splitlines()) if m]
    if not vs or vs[-1].group(1) != a.result:
        print("ERROR: the log's VERDICT (%s) does not match the requested result %s: refused" % (vs[-1].group(1) if vs else "none", a.result))
        sys.exit(2)
    if a.result == "PASS" and analyze_output(text, a.tc, 0)[0] != "PASS":
        print("ERROR: the analyzer does not say PASS on this log: refused")
        sys.exit(2)
    with open(path, "a", encoding="utf-8", newline="\n") as f:
        f.write("# RESULT,%s,%s,%s\n" % (a.result, logger.now_iso(), a.note or ""))
    logger.append_summary(a.tc, a.result, sprint=SPRINT, sha=sha, root=os.path.join(root, "test_logs") if root else None)
    print("recorded %s %s at %s" % (a.tc, a.result, sha))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("run"); p.add_argument("--tc", required=True); p.add_argument("--rehearsal", action="store_true"); p.set_defaults(fn=cmd_run)
    p = sub.add_parser("analyze"); p.add_argument("--log", required=True); p.set_defaults(fn=cmd_analyze)
    p = sub.add_parser("result"); p.add_argument("--tc", required=True)
    p.add_argument("--result", required=True, choices=["PASS", "FAIL", "BLOCKED"]); p.add_argument("--note"); p.set_defaults(fn=cmd_result)
    a = ap.parse_args(argv)
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
