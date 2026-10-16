#!/usr/bin/env python3
"""S1 runner: executes the S1 test cases and writes test_logs/ in the format of test plan 5.3.
S1 test cases (test plan section 6): TC-12 (host part), TC-42, TC-78, TC-79, TC-81.
  python tools/s1_run.py                                   # host evidence; TC-42/TC-78 end up BLOCKED
  python tools/s1_run.py --key-file C:\\keys\\vest01.key --build --ide 2.3.10 --docs docs   # owner machine
A verdict is PASS only when every clause of the pass rule was actually checked. Otherwise BLOCKED, with the reason."""
import argparse, csv, datetime, json, os, platform, re, shutil, subprocess, sys
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
PY = sys.executable
sys.path.insert(0, os.path.dirname(__file__))
import build_check, check_traceability

def sh(cmd, cwd=ROOT, env=None):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, env=env); return r.returncode, r.stdout + r.stderr

def main(argv=None):
    ap = argparse.ArgumentParser(); ap.add_argument("--key-file"); ap.add_argument("--build", action="store_true")
    ap.add_argument("--ide"); ap.add_argument("--docs", default=os.path.join(ROOT, "docs")); ap.add_argument("--tester", default="S1 engineer")
    ap.add_argument("--mutation", action="store_true"); ap.add_argument("--sprint", default="S1"); a = ap.parse_args(argv)
    sha = sh(["git", "rev-parse", "--short=7", "HEAD"])[1].strip() or "0000000"
    dirty = bool(sh(["git", "status", "--porcelain", "--", ".", ":(exclude)test_logs"])[1].strip())
    now = datetime.datetime.now().astimezone(); stamp = now.strftime("%Y%m%d-%H%M%S"); iso = now.isoformat(timespec="seconds")
    base = os.path.join(ROOT, "test_logs", a.sprint)
    def folder(tc):
        p = os.path.join(base, tc, "%s_%s" % (stamp, sha)); os.makedirs(p, exist_ok=True); return p
    def log_lines(text, device="host"):
        return "".join("%s, %s, %s\n" % (iso, device, l) for l in text.splitlines())

    # ---- shared evidence
    rc_make, out_make = sh(["make", "-C", os.path.join(ROOT, "test", "host"), "test"])
    tests = [l.split("\t") for l in out_make.splitlines() if l.startswith("TEST\t")]
    summ = [l for l in out_make.splitlines() if l.startswith("SUMMARY")]
    def by_tc(tc): return [t for t in tests if t[1] == tc]
    def all_pass(ts): return bool(ts) and all(t[3] == "PASS" for t in ts)
    rc_py, out_py = sh([PY, "-m", "unittest", "discover", "-s", os.path.join(ROOT, "tools", "tests"), "-p", "test_*.py"],
                       env=dict(os.environ, PPE_DOCS=a.docs))
    py_ok = rc_py == 0; py_n = re.search(r"Ran (\d+) tests", out_py)
    mut_txt = ""
    if a.mutation:
        rc_m, mut_txt = sh([PY, os.path.join(ROOT, "tools", "mutation_check.py")]); 
    dup = [p for p in sh(["git", "ls-files", "*ppe_proto.h"])[1].split() if p != "common/src/ppe_proto.h"]
    for root, dirs, files in os.walk(ROOT):
        dirs[:] = [d for d in dirs if d not in (".git", "build", "node_modules")]
        for f in files:
            rel = os.path.relpath(os.path.join(root, f), ROOT).replace("\\", "/")
            if f == "ppe_proto.h" and rel != "common/src/ppe_proto.h" and rel not in dup: dup.append(rel)
    results = []   # (tc, result, measurements, analysis, notes, extra_logs, tool_serial)
    toolchain_host = {"ide": "n/a (host run)", "esp32_core": "n/a (host run)", "nimble": "n/a (host run)"}

    # ---- TC-12 (host part)
    t12 = by_tc("TC-12"); ok12 = rc_make == 0 and all_pass(t12) and py_ok
    results.append(("TC-12", "PASS" if ok12 else "FAIL",
        {"scope": "host part only (S1). Bench part (node on air, sniffer, 1000-packet verifier run) is owed in S4",
         "host_tests": len(t12), "host_tests_passed": len([t for t in t12 if t[3] == "PASS"]),
         "appendix_a_vectors": "A.1 (21-byte adv, 17-byte payload, full digest) and A.2 (17-byte payload) reproduced" if ok12 else "see log",
         "sc02_verifier_unit_tests": "pass" if py_ok else "FAIL"},
        "\n".join("%s\t%s\t%s" % (t[2], t[3], t[4]) for t in t12) + "\nmake test rc=%d\n%s" % (rc_make, summ[0] if summ else ""),
        "Expected values come from tools/gen_vectors.py (typed from spec Appendix A and cross-checked with Python hmac), never from the code under test. "
        "Production wrapper uses mbedtls %s on the host; a portable SHA-256 oracle in test/host cross-checks 2000 random keys/messages. "
        "Device mbedtls (esp32 core 3.0.7) is a different build of the same API: only the S4 bench run proves it." % "(host system library)", [], None))

    # ---- TC-79 (inspection + unit test)
    t79 = by_tc("TC-79")
    hdr = open(os.path.join(ROOT, "common", "src", "ppe_proto.h"), encoding="utf-8").read()
    has_sa = bool(re.search(r"static_assert\(PPE_ADV_LEN == 4u \+ PPE_PAYLOAD_LEN", hdr)) and "PPE_ADV_LEN <= 31u" in hdr
    ok79 = all_pass(t79) and has_sa and not dup
    results.append(("TC-79", "PASS" if ok79 else "FAIL",
        {"scope": "inspection + host unit test (S1). On-air confirmation by sniffer is owed in S4",
         "static_assert_21_bytes_present": has_sa, "adv_length_bytes": 21, "other_copies_of_ppe_proto_h": dup},
        "unit tests: %s\nother copies of ppe_proto.h in the repo: %s" % ([t[2] + "=" + t[3] for t in t79], dup or "none"),
        ("Single ppe_proto.h check: " + ("OK, no other copy in this tree." if not dup else "FAIL, remove: %s" % dup)), [], None))

    # ---- TC-42
    sc_args = [shutil.which("bash") or "bash", os.path.join(ROOT, "tools", "secret_scan.sh")] + (["--key-file", a.key_file] if a.key_file else [])
    rc_sc, out_sc = sh(sc_args)
    build_res = None; blogdir = None
    if a.build:
        blogdir = folder("TC-78"); build_res = build_check.check(a.ide, None, blogdir)
    scan_clean = rc_sc == 0
    ok42 = scan_clean and a.key_file and build_res and build_res["result"] == "PASS"
    reason42 = []
    if not a.key_file: reason42.append("no --key-file given: the real-key scan over all commits was not run (generic pattern scan only)")
    if not (build_res and build_res["result"] == "PASS"): reason42.append("device build with no key in source not demonstrated (TC-78 build)")
    r42 = "FAIL" if not scan_clean else ("PASS" if ok42 else "BLOCKED")
    results.append(("TC-42", r42,
        {"scan_exit_code": rc_sc, "scan_mode": "key-file" if a.key_file else "generic pattern only",
         "gitignore_and_tracked_file_hygiene": "checked", "secrets_h_present_in_tree": os.path.exists(os.path.join(ROOT, "common", "src", "secrets.h")),
         "host_build_with_no_key_in_source": "ok" if rc_make == 0 else "FAIL", "blocked_because": reason42},
        out_sc, "TC-42 pass rule has two clauses: (1) 0 occurrences of the REAL key in all commits, (2) firmware builds with no key in source. "
        "Clause 1 needs your key file (kept outside the repo); clause 2 needs arduino-cli with the pinned toolchain. "
        "Run: python tools/s1_run.py --key-file <path> --build --ide 2.3.10", [], None))

    # ---- TC-78
    if build_res is None: r78, m78 = "BLOCKED", {"blocked_because": "toolchain build not requested (--build)"}
    else:
        r78 = build_res["result"]; m78 = {"versions": build_res["versions"], "builds": build_res["builds"], "reason": build_res["reason"]}
    results.append(("TC-78", r78, m78, json.dumps(build_res, indent=2) if build_res else "build not run",
        "Pass rule: clean build of node and hub with exactly the pinned versions. Not demonstrable in the S1 authoring sandbox: no arduino-cli, and the "
        "Espressif/Arduino download hosts are not reachable from it. tools/build_check.py is covered by fake-CLI unit tests only; its first real "
        "run is on your Windows machine. Fill tools/build_targets.txt with the spike sketch paths first.", [], None))

    # ---- write folders for the four, then TC-81 last
    def write(tc, result, meas, analysis, notes, extra, tool_serial, toolchain=None, devices=None, params=None):
        p = folder(tc)
        rj = {"tc": tc, "sprint": a.sprint, "date": iso, "tester": a.tester, "fw": {"node": "-", "hub": "-", "tools": sha},
              "toolchain": toolchain or toolchain_host, "devices": devices or {"host": "%s %s, Python %s" % (platform.system(), platform.release(), platform.python_version())},
              "parameters": params or {}, "result": result, "measurements": meas, "bugs": [], "findings": [], "artifacts": ["serial_host.log", "analysis.txt", "notes.md"]}
        with open(os.path.join(p, "result.json"), "w", encoding="utf-8") as f: json.dump(rj, f, indent=2); f.write("\n")
        with open(os.path.join(p, "serial_host.log"), "w", encoding="utf-8") as f: f.write(log_lines(tool_serial or analysis))
        with open(os.path.join(p, "analysis.txt"), "w", encoding="utf-8") as f: f.write(analysis.rstrip() + "\n")
        with open(os.path.join(p, "notes.md"), "w", encoding="utf-8") as f:
            f.write("# %s run %s_%s\n\n%s\n\nWorking tree dirty at run time: %s\nHost: %s\n" % (tc, stamp, sha, notes, "YES (results are indicative only)" if dirty else "no", rj["devices"]["host"]))
        return p
    sp = os.path.join(base, "..", "summary.csv"); sp = os.path.normpath(sp)
    new = not os.path.exists(sp)
    with open(sp, "a", newline="", encoding="utf-8") as f:
        w = csv.writer(f, lineterminator="\n")
        if new: w.writerow(["TC", "sprint", "date", "sha", "result"])
        for tc, res, meas, ana, notes, extra, ts in results:
            tcn = None
            tchain = {"ide": a.ide or "-", "esp32_core": (build_res or {}).get("versions", {}).get("esp32_core") or "-", "nimble": (build_res or {}).get("versions", {}).get("nimble") or "-"} if tc in ("TC-42", "TC-78") and build_res else None
            write(tc, res, meas, ana, notes, extra, ts, toolchain=tchain); w.writerow([tc, a.sprint, iso, sha, res])
    # TC-81 (script). Plain check first, then the sprint-completeness check once the other rows exist.
    rc_t, out_t = sh([PY, os.path.join(ROOT, "tools", "check_traceability.py"), "--docs", a.docs])
    docs_missing = "document not found" in out_t
    rc_t2, out_t2 = sh([PY, os.path.join(ROOT, "tools", "check_traceability.py"), "--docs", a.docs, "--sprint", a.sprint, "--test-logs", os.path.join(ROOT, "test_logs")])
    # the TC-81 row itself is appended below, so only that one finding is acceptable at this point
    only_self = [l for l in out_t2.splitlines() if l.startswith("FINDING")]
    sprint_ok = all("TC-81" in l for l in only_self)
    r81 = "BLOCKED" if docs_missing else ("PASS" if rc_t == 0 and sprint_ok else "FAIL")
    with open(sp, "a", newline="", encoding="utf-8") as f: csv.writer(f, lineterminator="\n").writerow(["TC-81", a.sprint, iso, sha, r81])
    write("TC-81", r81, {"requirements_with_test": out_t.splitlines()[0] if out_t else "", "sprint_summary_check": "ok (TC-81 row added after the check)" if sprint_ok else "FAIL"},
          out_t + "\n--- with --sprint %s ---\n%s" % (a.sprint, out_t2), "SC-09 run on the documents in %s (read only)." % a.docs, [], None)

    # host-suite log: every host test, with the TC it supports, plus mutation check
    hs = os.path.join(base, "host-suite", "%s_%s" % (stamp, sha)); os.makedirs(hs, exist_ok=True)
    with open(os.path.join(hs, "serial_host.log"), "w", encoding="utf-8") as f: f.write(log_lines(out_make + "\n" + out_py))
    with open(os.path.join(hs, "test_map.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, lineterminator="\n"); w.writerow(["tc", "test", "result", "checks"]); [w.writerow([t[1], t[2], t[3], t[4].replace("checks=", "")]) for t in tests]
    with open(os.path.join(hs, "notes.md"), "w", encoding="utf-8") as f:
        f.write("# Host suite %s_%s\n\nAll host tests with the TC each supports. Rows for TC-11, 17, 22, 23, 24, 32, 73, 75 are early host evidence for "
                "later sprints (S3, S4, S6): they are NOT passes of those TCs.\n\n```\n%s\n```\n" % (stamp, sha, (summ[0] if summ else "") + "\n" + (mut_txt or "mutation check not run")))
    if mut_txt:
        with open(os.path.join(hs, "mutation_check.txt"), "w", encoding="utf-8") as f: f.write(mut_txt)
    print("S1 run %s_%s%s" % (stamp, sha, "  (DIRTY TREE)" if dirty else ""))
    for tc, res, *_ in results: print("  %-6s %s" % (tc, res))
    print("  %-6s %s" % ("TC-81", r81))
    print("host tests: %s | tools tests: %s tests %s" % (summ[0].replace("\t", " ") if summ else "n/a", py_n.group(1) if py_n else "?", "ok" if py_ok else "FAILED"))
    return 1 if any(r[1] == "FAIL" for r in results) or r81 == "FAIL" else 0

if __name__ == "__main__": sys.exit(main())
