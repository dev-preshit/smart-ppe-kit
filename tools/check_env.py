#!/usr/bin/env python3
"""Environment check for the S1 workflow. Run on YOUR machine: python tools/check_env.py
Reports what is present and what each missing piece blocks. Exit 0 always (it is a report)."""
import os, platform, re, shutil, subprocess, sys, tempfile

def ver(cmd):
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=20); return (r.stdout + r.stderr).strip().splitlines()[0]
    except Exception: return None

def mbedtls_ok(cxx):
    src = '#include "mbedtls/md.h"\nint main(){return mbedtls_md_info_from_type(MBEDTLS_MD_SHA256)?0:1;}\n'
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "t.cpp"); open(p, "w").write(src)
        r = subprocess.run([cxx, p, "-lmbedcrypto", "-o", os.path.join(d, "t")], capture_output=True, text=True)
        return r.returncode == 0

rows = []
def row(name, found, blocks): rows.append((name, found or "MISSING", "" if found else blocks))
row("OS", "%s %s (%s)" % (platform.system(), platform.release(), platform.machine()), "")
cxx = shutil.which("g++") or shutil.which("clang++")
row("C++ compiler", ver([cxx, "--version"]) if cxx else None, "host tests (make test)")
row("make", ver(["make", "--version"]) if shutil.which("make") else None, "host tests (install MSYS2 'make' or use WSL)")
row("Python", "%s (%s)" % (platform.python_version(), sys.executable), "")
try:
    from importlib.metadata import version; row("python jsonschema", version("jsonschema"), "")
except ImportError: row("python jsonschema", None, "schema tests: pip install jsonschema")
row("mbedtls (host build)", "headers+lib usable by %s" % cxx if cxx and mbedtls_ok(cxx) else None, "host tests: MSYS2 'pacman -S mingw-w64-ucrt-x86_64-mbedtls' or WSL 'apt install libmbedtls-dev'")
row("git", ver(["git", "--version"]) if shutil.which("git") else None, "everything")
row("bash (Git Bash)", ver(["bash", "--version"]) if shutil.which("bash") else None, "tools/secret_scan.sh (TC-42)")
row("gitleaks", ver(["gitleaks", "version"]) if shutil.which("gitleaks") else None, "gitleaks half of TC-42")
cli = shutil.which("arduino-cli")
row("arduino-cli", ver(["arduino-cli", "version"]) if cli else None, "TC-78 and the device clause of TC-42")
if cli:
    core = subprocess.run(["arduino-cli", "core", "list"], capture_output=True, text=True).stdout
    lib = subprocess.run(["arduino-cli", "lib", "list"], capture_output=True, text=True).stdout
    m = re.search(r"esp32:esp32\s+(\S+)", core); row("esp32 core (need 3.0.7)", m.group(1) if m else None, "TC-78")
    m = re.search(r"NimBLE-Arduino\s+(\S+)", lib); row("NimBLE-Arduino (need 2.5.1)", m.group(1) if m else None, "TC-78")
w = max(len(r[0]) for r in rows)
for n, f, b in rows: print("%-*s  %s%s" % (w, n, f, ("   <- blocks: " + b) if b else ""))
