#!/usr/bin/env python3
"""Proves the host suite can fail: applies one deliberate bug at a time to a scratch copy of common/
and requires `make test` to report at least one failing test. Exit 1 if any mutant survives."""
import os, shutil, subprocess, sys, tempfile
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
MUTANTS = [  # (name, file, old, new)
 ("tag uses wrong bytes", "ppe_crypto.cpp", "tag[i] = full[i]", "tag[i] = full[i+1]"),
 ("ct_equal compares 1 byte", "ppe_crypto.cpp", "for (size_t i = 0; i < n; i++) diff", "for (size_t i = 0; i < 1 && i < n; i++) diff"),
 ("block extension not persisted", "ppe_counter.cpp", "if (!store_->write_ceiling(nc)) return PPE_CTR_ERR_NVS;\n    ceiling_ = nc;", "ceiling_ = nc;"),
 ("cold boot does not advance ceiling", "ppe_counter.cpp", "uint32_t nc = advance_ceiling(stored);", "uint32_t nc = stored;"),
 ("counter limit not enforced", "ppe_counter.cpp", "if ((uint64_t)counter_ + 1u > PPE_COUNTER_LIMIT) return PPE_CTR_ERR_LIMIT;", ""),
 ("state checked before HMAC", "ppe_filter.cpp", "  if (!ppe_hmac_verify(key, pl)) return PPE_RX_HMAC_FAIL;\n", "  if (!ppe_state_valid(out->state)) return PPE_RX_DROP_STATE;\n  if (!ppe_hmac_verify(key, pl)) return PPE_RX_HMAC_FAIL;\n"),
 ("version checked after HMAC (removed)", "ppe_filter.cpp", "if (ppe_flags_version(out->flags) != PPE_PROTO_VER) return PPE_RX_DROP_VER;", ""),
 ("duplicate accepted", "ppe_filter.cpp", "if (c > last)", "if (c >= last)"),
 ("counter big-endian", "ppe_proto.cpp", "p[0] = (uint8_t)(v);       p[1] = (uint8_t)(v >> 8);", "p[1] = (uint8_t)(v);       p[0] = (uint8_t)(v >> 8);"),
 ("hmac verify always true", "ppe_crypto.cpp", "return ppe_ct_equal(tag, payload + PPE_OFF_HMAC, PPE_HMAC_LEN);", "return true;"),
]
def main():
    survivors = 0
    for name, f, old, new in MUTANTS:
        with tempfile.TemporaryDirectory() as t:
            for d in ("common", "tools", "test"):
                shutil.copytree(os.path.join(ROOT, d), os.path.join(t, d), ignore=shutil.ignore_patterns("build", "gen", "__pycache__"))
            p = os.path.join(t, "common", "src", f); s = open(p).read()
            if old not in s: print("MUTATION NOT APPLICABLE:", name); survivors += 1; continue
            open(p, "w").write(s.replace(old, new, 1))
            r = subprocess.run(["make", "-C", os.path.join(t, "test", "host"), "test"], capture_output=True, text=True)
            summ = [l for l in r.stdout.splitlines() if l.startswith("SUMMARY")]
            if not summ: print("%-40s BUILD ERROR (counts as not killed by tests)" % name); survivors += 1; continue
            failed = int(summ[0].split("failed=")[1].split("\t")[0])
            print("%-40s %s (%d failing tests)" % (name, "KILLED" if failed else "SURVIVED", failed))
            survivors += 0 if failed else 1
    print("mutants=%d survivors=%d" % (len(MUTANTS), survivors))
    return 1 if survivors else 0
if __name__ == "__main__": sys.exit(main())
