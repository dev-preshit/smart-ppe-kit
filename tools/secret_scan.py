#!/usr/bin/env python3
"""SC-10 engine (called by secret_scan.sh). Looks for the real vest key in ALL git history and the working
tree (including ignored files) in these forms: hex (lower/upper), spaced hex bytes, colon hex, C array
(any wrapping, 0x.. or \\x.. style), decimal byte list, base64 (std/urlsafe), and raw bytes (binary blobs).
Without --key-file it runs a generic scan for any 32-byte key-looking material that is not allow-listed.
The key and matched text are NEVER printed: only form, commit, path.
Exit: 0 clean, 1 finding, 2 setup error."""
import argparse, base64, os, re, subprocess, sys

TEST_KEY = bytes(range(32))
ALLOW = {TEST_KEY, bytes(32),                                   # spec Appendix A test key, all-zero placeholder
         bytes.fromhex("3222111bd50a5b34d531d0ce9aa7dbd39f985e938c82f9c6f10158d5733ca3c3")}  # A.1 digest (not a key)

def git(repo, *a, text=True):
    return subprocess.run(["git", "-C", repo, *a], capture_output=True, text=text)

def load_key(path):
    raw = open(path, "rb").read().strip()
    if len(raw) == 32: return raw
    try:
        k = bytes.fromhex(raw.decode().replace(" ", "").replace("\n", "").replace("\r", ""))
        if len(k) == 32: return k
    except Exception: pass
    sys.exit("key file must hold 32 raw bytes or 64 hex characters"); 

def norm_hex_stream(t):
    """Text -> lowercase hex digits only after dropping 0x / \\x prefixes and separators."""
    t = t.lower(); t = re.sub(r"0x|\\x", "", t)
    return re.sub(r"[^0-9a-f]", "", t)

def forms(key):
    """name -> function(text/bytes) -> bool"""
    h = key.hex(); b64 = base64.b64encode(key).decode(); b64u = base64.urlsafe_b64encode(key).decode()
    dec = ",".join(str(x) for x in key)
    def hexstream(t): return h in norm_hex_stream(t)
    return {
        "hex/spaced/colon/C-array": lambda t, raw: hexstream(t),
        "base64": lambda t, raw: (b64 in re.sub(r"\s", "", t)) or (b64.rstrip("=") in re.sub(r"\s", "", t)) or (b64u.rstrip("=") in re.sub(r"\s", "", t)),
        "decimal-list": lambda t, raw: dec in re.sub(r"[\s{}\[\]()]", "", t),
        "raw-bytes": lambda t, raw: key in raw,
    }

def generic_hits(raw):
    """Key-looking material for pattern mode. Returns list of 32-byte values not allow-listed."""
    t = raw.decode("latin-1"); hits = []
    for m in re.finditer(r"(?<![0-9A-Fa-f])[0-9A-Fa-f]{64}(?![0-9A-Fa-f])", t):
        v = bytes.fromhex(m.group(0))
        if v not in ALLOW: hits.append("64-hex")
    for m in re.finditer(r"(?:(?:0x[0-9A-Fa-f]{2})\s*,\s*){31}0x[0-9A-Fa-f]{2}", t):
        v = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", m.group(0)))
        if v not in ALLOW: hits.append("32-byte C array")
    return hits

def all_blobs(repo):
    """(commit-or-WORKTREE, path, bytes) over every blob reachable from any ref, plus the working tree."""
    seen = set()
    revs = git(repo, "rev-list", "--all").stdout.split()
    for rev in revs:
        for line in git(repo, "ls-tree", "-r", rev).stdout.splitlines():
            meta, path = line.split("\t", 1); sha = meta.split()[2]
            if sha in seen: continue
            seen.add(sha)
            data = subprocess.run(["git", "-C", repo, "cat-file", "blob", sha], capture_output=True).stdout
            yield rev[:7], path, data
    # staged-but-uncommitted
    for line in git(repo, "ls-files", "-s").stdout.splitlines():
        meta, path = line.split("\t", 1); sha = meta.split()[1]
        if sha in seen: continue
        seen.add(sha)
        yield "INDEX", path, subprocess.run(["git", "-C", repo, "cat-file", "blob", sha], capture_output=True).stdout
    for root, dirs, files in os.walk(repo):
        dirs[:] = [d for d in dirs if d not in (".git", "build", "__pycache__", "node_modules")]
        for f in files:
            p = os.path.join(root, f)
            try:
                if os.path.getsize(p) > 20_000_000: continue
                yield "WORKTREE", os.path.relpath(p, repo), open(p, "rb").read()
            except OSError: pass
    # commit messages too
    for rev in revs:
        yield rev[:7], "<commit message>", git(repo, "log", "-1", "--format=%B%n%an%n%ae", rev, text=False).stdout

def main(argv=None):
    ap = argparse.ArgumentParser(); ap.add_argument("--repo", default="."); ap.add_argument("--key-file")
    a = ap.parse_args(argv); repo = os.path.abspath(a.repo)
    if git(repo, "rev-parse", "--git-dir").returncode != 0: print("not a git repository:", repo); return 2
    findings = []
    if a.key_file:
        key = load_key(a.key_file); F = forms(key); mode = "key-file"
        for where, path, raw in all_blobs(repo):
            if where == "WORKTREE" and os.path.abspath(os.path.join(repo, path)) == os.path.abspath(a.key_file): continue
            txt = raw.decode("latin-1")
            for name, fn in F.items():
                if fn(txt, raw): findings.append((name, where, path))
    else:
        mode = "pattern-only (no key file: TC-42 needs a key-file run on the owner's machine)"
        for where, path, raw in all_blobs(repo):
            for h in generic_hits(raw): findings.append((h, where, path))
    print("secret_scan.py mode:", mode)
    for name, where, path in sorted(set(findings)): print("FINDING  form=%s  at=%s  path=%s" % (name, where, path))
    print("RESULT", "FAIL (%d finding(s))" % len(set(findings)) if findings else "PASS (0 occurrences)")
    return 1 if findings else 0

if __name__ == "__main__": sys.exit(main())
