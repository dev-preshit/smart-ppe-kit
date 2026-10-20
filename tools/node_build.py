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
import argparse, os, shutil, subprocess, sys, tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKETCH = os.path.join(REPO, "firmware", "node_sense")
FQBN = "esp32:esp32:esp32c3:CDCOnBoot=cdc"
TYPES = {"foot": 0, "glove": 1, "helmet": 2}


def build_opt_lines(node_type, hooks, adc_boot=None, closed_level=None):
    lines = ["-DPPE_BUILD_NODE_TYPE=%d" % TYPES[node_type]]
    if hooks:
        lines.append("-DPPE_TEST_HOOKS")
    if adc_boot is not None:
        lines.append("-DPPE_TEST_ADC_BOOT=%d" % adc_boot)
    if closed_level is not None:
        lines.append("-DPPE_CONTACT_CLOSED_LEVEL=%d" % closed_level)
    return lines


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
    opts = build_opt_lines(a.type, a.hooks, a.adc_boot, a.closed_level)
    tmp = tempfile.mkdtemp(prefix="node_build_")
    sk = os.path.join(tmp, "node_sense")
    bld = os.path.join(tmp, "build")
    os.makedirs(sk)
    shutil.copy(os.path.join(SKETCH, "node_sense.ino"), sk)
    with open(os.path.join(sk, "build_opt.h"), "w", newline="\n") as f:
        f.write("\n".join(opts) + "\n")
    print("variant:", a.type, "| hooks" if a.hooks else "| RELEASE", "|", " ".join(opts))
    cmd = ["arduino-cli", "compile", "--fqbn", FQBN, "--build-path", bld, sk]
    print(" ".join(cmd))
    if a.dry_run:
        return 0
    rc = 1
    try:
        rc = subprocess.run(cmd).returncode
        if rc != 0:
            print("BUILD FAILED, folder kept:", tmp)
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
