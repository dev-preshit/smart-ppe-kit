#!/usr/bin/env python3
"""S5 hub bench build and flash with the firmware identity (RULES section 4). TEST TOOL.

The hub prints 'FW hub=<8 hex> common=<8 hex> dirty=<0|1> hooks=<0|1>' at boot and before every dump. The two values
are the first 8 hex digits of the git tree hashes of firmware/hub and common at HEAD: they change only when the
firmware sources change, so a commit that touches only tools or docs keeps the flashed firmware valid.
s5_bench.open_run (used by s5_bench.py and s5_motor.py) asks the hub for this line and stops before the run if it
differs from HEAD, says dirty, or is missing.

  python tools/s5_build.py id                       # the identity a hub built from HEAD must print
  python tools/s5_build.py build                    # clean bench build; refuses uncommitted firmware changes
  python tools/s5_build.py upload --port COM3       # flash that build; refuses any port except the hub port COM3
"""
import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
HUB_DIR = os.path.join(REPO, "firmware", "hub")
COMMON_DIR = os.path.join(REPO, "common")
BUILD_DIR = os.path.join(REPO, "build", "hub_bench")          # git-ignored (build/)
ID_FILE = os.path.join(BUILD_DIR, "fw_identity.txt")
FQBN = "esp32:esp32:esp32s3:CDCOnBoot=default"                # COM connector (CH343), RULES section 4
HUB_PORTS = ("COM3",)                                         # DECISIONS: the hub board is S5's, on the CH343 port


def git(*args):
    r = subprocess.run(["git"] + list(args), cwd=REPO, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError("git %s failed: %s" % (" ".join(args), r.stderr.strip()))
    return r.stdout.strip()


def fw_identity():
    """(hub tree hash, common tree hash, dirty) as (int, int, 0|1) for HEAD."""
    hub = int(git("rev-parse", "HEAD:firmware/hub")[:8], 16)
    common = int(git("rev-parse", "HEAD:common")[:8], 16)
    dirty = 1 if git("status", "--porcelain", "--untracked-files=no", "--", "firmware/hub", "common") else 0
    return hub, common, dirty


def fmt(ident):
    return "hub=%08x common=%08x dirty=%d" % ident


def cli():
    for c in (os.environ.get("ARDUINO_CLI"), shutil.which("arduino-cli"), r"C:\tools\arduino-cli\arduino-cli.exe"):
        if c and os.path.exists(c):
            return c
    sys.exit("ERROR: arduino-cli not found (set ARDUINO_CLI)")


def bench_opts_ok():
    """The git-ignored build_opt.h must be the committed bench copy (otherwise the build is not the reviewed one)."""
    try:
        with open(os.path.join(HUB_DIR, "build_opt.h"), "rb") as f:
            cur = f.read()
        with open(os.path.join(HUB_DIR, "build_opt.h.bench"), "rb") as f:
            ref = f.read()
    except OSError:
        return False
    return cur == ref


def cmd_id(a):
    print("expected FW line: FW %s hooks=1" % fmt(fw_identity()))
    return 0


def cmd_build(a):
    ident = fw_identity()
    print("firmware id:", fmt(ident))
    if ident[2]:
        print("ERROR: firmware/hub or common has uncommitted changes: commit first, the identity must name HEAD")
        return 2
    if not bench_opts_ok():
        print("ERROR: firmware/hub/build_opt.h is missing or differs from build_opt.h.bench")
        return 2
    flags = "-DPPE_FW_HUB=0x%08x -DPPE_FW_COMMON=0x%08x -DPPE_FW_DIRTY=0" % ident[:2]
    os.makedirs(BUILD_DIR, exist_ok=True)
    cmd = [cli(), "compile", "--clean", "--fqbn", FQBN, "--library", COMMON_DIR, "--build-path", BUILD_DIR,
           "--build-property", "compiler.cpp.extra_flags=" + flags, HUB_DIR]
    r = subprocess.run(cmd)
    if r.returncode != 0:
        print("ERROR: compile failed (exit %d)" % r.returncode)
        return 1
    with open(ID_FILE, "w") as f:
        f.write(fmt(ident) + "\n")
    print("build ok:", BUILD_DIR, "->", fmt(ident))
    return 0


def cmd_upload(a):
    if a.port.upper() not in HUB_PORTS:
        print("ERROR: %s is not the hub port (%s): refused" % (a.port, ", ".join(HUB_PORTS)))
        return 2
    ident = fw_identity()
    try:
        built = open(ID_FILE).read().strip()
    except OSError:
        built = ""
    if built != fmt(ident) or ident[2]:
        print("ERROR: the build (%s) is not the one of HEAD (%s): run build first" % (built or "none", fmt(ident)))
        return 2
    r = subprocess.run([cli(), "upload", "-p", a.port, "--fqbn", FQBN, "--input-dir", BUILD_DIR])
    if r.returncode != 0:
        print("ERROR: upload failed (exit %d)" % r.returncode)
        return 1
    print("flashed %s with %s; the hub prints 'FW %s hooks=1' at boot and before every dump" % (a.port, fmt(ident), fmt(ident)))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("id").set_defaults(fn=cmd_id)
    sub.add_parser("build").set_defaults(fn=cmd_build)
    p = sub.add_parser("upload"); p.add_argument("--port", required=True); p.set_defaults(fn=cmd_upload)
    a = ap.parse_args(argv)
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
