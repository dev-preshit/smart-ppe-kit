#!/usr/bin/env bash
# SC-10 secret_scan.sh - real-key scan over ALL commits and the working tree (TC-42, FR-42, NFR-04).
#   tools/secret_scan.sh --key-file C:/keys/vest01.key      (owner machine: the real check)
#   tools/secret_scan.sh                                    (CI: generic pattern scan, no key available)
# Also: .gitignore / tracked-file hygiene, and gitleaks when installed. Exit 0 clean, 1 finding, 2 setup error.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"; KEYARGS=()
KEYFILE="${PPE_KEY_FILE:-}"
while [ $# -gt 0 ]; do
  case "$1" in --key-file) KEYFILE="$2"; shift 2;; --repo) REPO="$2"; shift 2;; *) echo "usage: $0 [--key-file PATH] [--repo DIR]"; exit 2;; esac
done
[ -n "$KEYFILE" ] && KEYARGS=(--key-file "$KEYFILE")
PY=python3; command -v python3 >/dev/null 2>&1 || PY=python
rc=0

echo "== 1. gitignore and tracked-file hygiene"
for rule in 'secrets.h' '*.key' 'keys/'; do
  grep -qxF "$rule" "$REPO/.gitignore" 2>/dev/null || grep -qE "^(/)?([a-z/]*/)?${rule//\*/\\*}\$" "$REPO/.gitignore" 2>/dev/null \
    || { echo "FINDING  missing .gitignore rule: $rule"; rc=1; }
done
bad=$(git -C "$REPO" ls-files | grep -E '(^|/)(secrets\.h|.*\.key|.*\.pem)$' || true)
[ -n "$bad" ] && { echo "FINDING  tracked secret-like files:"; echo "$bad"; rc=1; }
[ -f "$REPO/common/src/secrets.example.h" ] || { echo "FINDING  placeholder secrets.example.h missing"; rc=1; }
[ "$rc" = 0 ] && echo "ok"

echo "== 2. key scan over all commits + working tree"
"$PY" "$HERE/secret_scan.py" --repo "$REPO" "${KEYARGS[@]}"; r=$?
[ $r -ne 0 ] && rc=$r

echo "== 3. gitleaks"
if command -v gitleaks >/dev/null 2>&1; then
  gitleaks detect --source "$REPO" --no-banner --redact >/dev/null 2>&1 && echo "gitleaks: clean" || { echo "FINDING  gitleaks reported leaks (run it locally for details)"; rc=1; }
else
  echo "gitleaks: not installed here (skipped) - install it for the owner-machine TC-42 run"
fi
[ "$rc" = 0 ] && echo "SECRET SCAN: PASS" || echo "SECRET SCAN: FAIL (exit $rc)"
exit $rc
