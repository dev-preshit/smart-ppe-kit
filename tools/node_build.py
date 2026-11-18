#!/usr/bin/env python3
"""node_build.py - build (and optionally upload) firmware/node_sense for one node variant.

Flags go through a build_opt.h written into a temporary COPY of the sketch (the way the S0 spikes do it), so the
committed sketch folder stays clean and a release build can never pick up test flags. Run it in the same shell
you use for arduino-cli.

  python tools/node_build.py --type foot --hooks                 # FSR node, test hooks
  python tools/node_build.py --type foot --hooks --adc-boot 800  # TC-03 step 2: power-on reading 800
  python tools/node_build.py --type glove --hooks --closed-level 1
  python tools/node_build.py --type foot                         # release build, checks that no hook text is inside
  add --upload COM3 to flash the board (do not run logger.py or drive_contacts.py on that port at the same time)
"""
import argparse, os, re, shutil, subprocess, sys, tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKETCH = os.path.join(REPO, "firmware", "node_sense")
COMMON = os.path.join(REPO, "common")     # this checkout's own ppe_common, never the Arduino user-library link
FQBN = "esp32:esp32:esp32c3:CDCOnBoot=cdc"
TYPES = {"foot": 0, "glove": 1, "helmet": 2}
ALLOWED_UPLOAD_PORTS = ("COM9", "COM6")   # S4 bench boards only (A-23): COM9 node, COM6 observer / stand-in. Check list_ports before every flash.


def build_opt_lines(node_type, hooks, adc_boot=None, closed_level=None):
    lines = ["-DPPE_BUILD_NODE_TYPE=%d" % TYPES[node_type]]
    if hooks:
        lines.append("-DPPE_TEST_HOOKS")
    if adc_boot is not None:
        lines.append("-DPPE_TEST_ADC_BOOT=%d" % adc_boot)
    if closed_level is not None:
        lines.append("-DPPE_CONTACT_CLOSED_LEVEL=%d" % closed_level)
    return lines


def fw_identity(repo=REPO):
    """(tree hash of common/, tree hash of firmware/node_sense, dirty) at HEAD as numbers. dirty = 1 when the working tree differs
    from HEAD in those two folders, or when git cannot tell. The node prints it and observe.py refuses a mismatch."""
    def git(*args):
        try:
            r = subprocess.run(["git", "-C", repo] + list(args), capture_output=True, text=True, errors="replace")
        except OSError:
            return None
        return r.stdout.strip() if r.returncode == 0 else None
    a, b = git("rev-parse", "HEAD:common"), git("rev-parse", "HEAD:firmware/node_sense")
    st = git("status", "--porcelain", "--", "common", "firmware/node_sense")
    try:
        return int(a[:8], 16), int(b[:8], 16), 0 if st == "" else 1
    except (TypeError, ValueError):
        return 0, 0, 1


def used_library_path(output, name="ppe_common"):
    """Path arduino-cli printed in its 'Used library' table for the library, or None."""
    for line in output.splitlines():
        m = re.search(r"Using library %s at version \S+ in folder: (.+?)\s*$" % re.escape(name), line)   # --verbose
        if not m:
            m = re.match(r"^%s\s+\S+\s+(.+?)\s*$" % re.escape(name), line)                              # summary table
        if m:
            return m.group(1)
    return None


def same_path(a, b):
    n = lambda p: os.path.normcase(os.path.realpath(p))
    return n(a) == n(b)


def check_library(output, common=COMMON):
    """(ok, message): the build must have used THIS checkout's common/."""
    got = used_library_path(output)
    if got is None:
        return False, "no 'ppe_common' line in the compiler output, cannot confirm which common/ was used"
    if not same_path(got, common):
        return False, "built with the WRONG ppe_common: %s (expected %s)" % (got, common)
    return True, "ppe_common from this checkout: %s" % got


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--type", choices=sorted(TYPES), default="foot")
    ap.add_argument("--hooks", action="store_true", help="define PPE_TEST_HOOKS")
    ap.add_argument("--adc-boot", type=int, help="power-on ADC value (needs --hooks)")
    ap.add_argument("--closed-level", type=int, choices=[0, 1], help="pin level when a contact is closed (default 0)")
    ap.add_argument("--upload", metavar="PORT")
    ap.add_argument("--keep", action="store_true", help="keep the temporary build folder")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args(argv)
    if a.adc_boot is not None and not a.hooks:
        ap.error("--adc-boot needs --hooks")
    if a.upload and a.upload.upper() not in ALLOWED_UPLOAD_PORTS:
        ap.error("refusing to flash %s: S4 may only flash %s (A-23). Check python -m serial.tools.list_ports -v first." % (a.upload, " or ".join(ALLOWED_UPLOAD_PORTS)))
    opts = build_opt_lines(a.type, a.hooks, a.adc_boot, a.closed_level)
    ident = fw_identity()
    opts += ["-DPPE_FW_COMMON=0x%08x" % ident[0], "-DPPE_FW_NODE=0x%08x" % ident[1], "-DPPE_FW_DIRTY=%d" % ident[2]]
    print("firmware id: common=%08x node=%08x dirty=%d" % ident)
    if ident[2]:
        print("WARNING: common/ or firmware/node_sense differ from HEAD (or git failed): the node will report dirty=1 and observe.py capture will refuse it. Commit first.")
    tmp = tempfile.mkdtemp(prefix="node_build_")
    sk = os.path.join(tmp, "node_sense")
    bld = os.path.join(tmp, "build")
    os.makedirs(sk)
    shutil.copy(os.path.join(SKETCH, "node_sense.ino"), sk)
    with open(os.path.join(sk, "build_opt.h"), "w", newline="\n") as f:
        f.write("\n".join(opts) + "\n")
    print("variant:", a.type, "| hooks" if a.hooks else "| RELEASE", "|", " ".join(opts))
    cmd = ["arduino-cli", "compile", "--fqbn", FQBN, "--verbose", "--library", COMMON, "--build-path", bld, sk]
    print(" ".join(cmd))
    if a.dry_run:
        return 0
    rc = 1
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
        text = p.stdout + p.stderr
        rc = p.returncode
        if rc != 0:
            print("\n".join(text.splitlines()[-80:]))          # verbose output is long: the tail has the error
            print("BUILD FAILED, folder kept:", tmp)
            return rc
        for line in text.splitlines():
            if line.startswith(("Sketch uses", "Global variables")):
                print(line)
        ok, msg = check_library(text)
        print(msg)
        if not ok:
            rc = 3
            return rc
        elf = os.path.join(bld, "node_sense.ino.elf")
        if not a.hooks and os.path.exists(elf):
            data = open(elf, "rb").read()
            bad = [t for t in (b"MARK ", b"CFG type=", b"adc fail") if t in data]
            print("release check:", "FAILED, found " + ", ".join(t.decode() for t in bad) if bad else "no hook text in the binary")
            if bad:
                return 1
        if a.upload:
            rc = subprocess.run(["arduino-cli", "upload", "-p", a.upload, "--fqbn", FQBN, "--input-dir", bld, sk]).returncode
            if rc != 0:
                print("UPLOAD FAILED")
                return rc
            print("uploaded to", a.upload)
        return 0
    finally:
        if not a.keep and rc == 0:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
