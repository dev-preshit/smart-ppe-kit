#!/usr/bin/env python3
"""TC-78 / NFR-09 build check with arduino-cli. Verifies the pinned versions, does a clean build of every
target in tools/build_targets.txt with common/ as the library and NO key in source, and confirms the
release build shows no test-hook strings (compile log and binary). It cannot see a -D flag that comes from a sketch's build_opt.h.
Exit 0 PASS, 1 FAIL, 3 BLOCKED (tool or targets missing).
Pinned (TOOLCHAIN.md Config A): esp32 core 3.0.7, NimBLE-Arduino 2.5.1, IDE 2.3.10 (IDE version is
attested with --ide because arduino-cli cannot read it)."""
import argparse, json, os, re, shutil, subprocess, sys, tempfile
PIN = {"esp32": "3.0.7", "nimble": "2.5.1", "ide": "2.3.10"}
HOOK_STRINGS = [b"setcounter", b"nvslog", b"skiptx", b"PPE_TEST_HOOKS"]
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

def run(cmd, cwd=None):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True); return r.returncode, r.stdout + r.stderr

def targets(path):
    out = []
    for l in open(path, encoding="utf-8"):
        l = l.strip()
        if l and not l.startswith("#"):
            n, s, f = [x.strip() for x in l.split("|")]; out.append((n, s, f))
    return out

def check(ide=None, targets_file=None, logdir=None, cli="arduino-cli"):
    res = {"result": "BLOCKED", "reason": "", "versions": {}, "builds": {}, "log_files": []}
    if shutil.which(cli) is None: res["reason"] = "arduino-cli not found on PATH"; return res
    rc, out = run([cli, "version"]); res["versions"]["arduino_cli"] = out.strip().splitlines()[0] if out.strip() else "?"
    rc, core = run([cli, "core", "list"]); m = re.search(r"esp32:esp32\s+(\S+)", core); res["versions"]["esp32_core"] = m.group(1) if m else None
    rc, libs = run([cli, "lib", "list"]); m = re.search(r"NimBLE-Arduino\s+(\S+)", libs); res["versions"]["nimble"] = m.group(1) if m else None
    res["versions"]["ide_attested"] = ide
    bad = []
    if res["versions"]["esp32_core"] != PIN["esp32"]: bad.append("esp32 core is %s, need %s" % (res["versions"]["esp32_core"], PIN["esp32"]))
    if res["versions"]["nimble"] != PIN["nimble"]: bad.append("NimBLE-Arduino is %s, need %s" % (res["versions"]["nimble"], PIN["nimble"]))
    if ide != PIN["ide"]: bad.append("IDE version not attested as %s (use --ide %s)" % (PIN["ide"], PIN["ide"]))
    if os.path.exists(os.path.join(ROOT, "common", "src", "secrets.h")): bad.append("common/src/secrets.h exists: build must have no key in source")
    ts = targets(targets_file or os.path.join(ROOT, "tools", "build_targets.txt"))
    if not ts: res["reason"] = "no active targets in build_targets.txt"; return res
    if bad: res["result"] = "FAIL"; res["reason"] = "; ".join(bad); return res
    allok = True
    for name, sketch, fqbn in ts:
        bp = tempfile.mkdtemp(prefix="ppe_build_")
        rc, log = run([cli, "compile", "--fqbn", fqbn, "--library", os.path.join(ROOT, "common"), "--build-path", bp, "--clean",
                       "--verbose", os.path.join(ROOT, sketch)], cwd=ROOT)
        entry = {"compiled": rc == 0, "test_hooks_in_build": None}
        if logdir:
            os.makedirs(logdir, exist_ok=True); lp = os.path.join(logdir, "build_%s.log" % name)
            with open(lp, "w", encoding="utf-8") as f: f.write(log)
            res["log_files"].append(lp)
        if rc == 0:
            hooks = "PPE_TEST_HOOKS" in log
            for root, _, files in os.walk(bp):
                for fn in files:
                    if fn.endswith(".elf"):
                        with open(os.path.join(root, fn), "rb") as f: blob = f.read()
                        hooks = hooks or any(h in blob for h in HOOK_STRINGS)
            entry["test_hooks_in_build"] = hooks
        shutil.rmtree(bp, ignore_errors=True)
        res["builds"][name] = entry; allok = allok and entry["compiled"] and entry["test_hooks_in_build"] is False
    res["result"] = "PASS" if allok else "FAIL"
    if not allok: res["reason"] = "a build failed or contains test hooks"
    return res

def main(argv=None):
    ap = argparse.ArgumentParser(); ap.add_argument("--ide"); ap.add_argument("--targets"); ap.add_argument("--logdir"); ap.add_argument("--cli", default="arduino-cli")
    a = ap.parse_args(argv); r = check(a.ide, a.targets, a.logdir, a.cli); print(json.dumps(r, indent=2))
    return {"PASS": 0, "FAIL": 1, "BLOCKED": 3}[r["result"]]
if __name__ == "__main__": sys.exit(main())
