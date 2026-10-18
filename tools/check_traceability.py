#!/usr/bin/env python3
"""SC-09 check_traceability.py (NFR-12, TC-81). Cross-checks SRS, traceability, test plan and state machines.
  1. every SRS requirement ID appears in traceability with a test ID (and nothing unknown appears)
  2. every test ID has a test-plan section with Setup, Steps, Tools, Pass and a sprint
  3. every test-plan Pass line equals the SRS acceptance criterion word for word (first listed requirement)
  4. every state-machine transition cites valid requirement IDs and test IDs
  5. no pending marker and no reference to an old document name in any of the five documents
  6. with --sprint Sn: test_logs/summary.csv has a valid row for every TC assigned to that sprint
Documents are found in --docs under either name style (SRS_v1.2.md or SRS_v1_2.md). Documents are read only."""
import argparse, csv, os, re, sys

DOCS = {"srs": "SRS_v1.2.md", "trace": "traceability_v1.2.md", "plan": "test_plan_v1.md",
        "sm": "state_machines_v1.md", "spec": "protocol_spec_v1.1.md"}

def find(docs, name):
    for n in (name, name.replace(".", "_", name.count(".") - 1)):
        p = os.path.join(docs, n)
        if os.path.exists(p): return p
    sys.exit("document not found in %s: %s" % (docs, name))

def cells(line): return [c.strip() for c in line.strip().strip("|").split("|")]

def split_row(line):
    # split on | that is not escaped as \|
    parts = re.split(r"(?<!\\)\|", line.strip())[1:-1]
    return [c.strip() for c in parts]

def main(argv=None):
    ap = argparse.ArgumentParser(); ap.add_argument("--docs", default="docs"); ap.add_argument("--sprint")
    ap.add_argument("--test-logs", default="test_logs"); a = ap.parse_args(argv)
    T = {}
    for k, v in DOCS.items():
        with open(find(a.docs, v), encoding="utf-8") as fh: T[k] = fh.read()
    errs, notes = [], []

    # SRS requirements and ACs
    srs = {}
    for l in T["srs"].splitlines():
        m = re.match(r"\|\s*((?:FR|NFR)-\d+)\s*\|", l)
        if m: c = split_row(l); srs[m.group(1)] = (c[1], c[2] if len(c) > 2 else "")
    decisions = set(re.findall(r"^\|\s*(D-\d+)\s*\|", T["srs"], re.M))
    # traceability
    trace = {}
    for l in T["trace"].splitlines():
        m = re.match(r"\|\s*((?:FR|NFR)-\d+)\s*\|", l)
        if m: c = split_row(l); trace[m.group(1)] = re.findall(r"TC-\d+", c[2])
    for r in srs:
        if r not in trace: errs.append("SRS %s not in traceability" % r)
        elif not trace[r]: errs.append("SRS %s has no test ID in traceability" % r)
    for r in trace:
        if r not in srs: errs.append("traceability lists unknown requirement %s" % r)
    tcs = sorted({t for v in trace.values() for t in v})
    # test plan sections
    secs = {}
    for m in re.finditer(r"^### (TC-\d+) (.*?)\n(.*?)(?=^### |^## |\Z)", T["plan"], re.M | re.S):
        secs[m.group(1)] = (m.group(2), m.group(3))
    for t in tcs:
        if t not in secs: errs.append("%s has no test-plan section" % t); continue
        head, body = secs[t]
        for field in ("Setup", "Steps", "Tools", "Pass"):
            if not re.search(r"^- \*\*%s:\*\*" % field, body, re.M): errs.append("%s missing %s" % (t, field))
        if not re.search(r"·[^·]*·\s*.*S\d", head): errs.append("%s heading has no sprint" % t)
    for t in secs:
        if t not in tcs: errs.append("test plan has %s not in traceability" % t)
    # Pass == SRS AC (word for word)
    for t, (head, body) in secs.items():
        reqs = re.findall(r"(?:FR|NFR)-\d+", head.split("·")[0])
        pm = re.search(r"^- \*\*Pass:\*\* (.*)$", body, re.M)
        if not pm or not reqs: continue
        primary = [r for r in reqs if r != "NFR-13"] or reqs
        ac = srs.get(primary[0], ("", ""))[1]
        if pm.group(1).strip() != ac.strip():
            errs.append("%s Pass line differs from %s acceptance criterion" % (t, primary[0]))
    # state machines
    nrows = 0
    for blk in re.finditer(r"^\|[^\n]*\bReq\b[^\n]*\n\|[-| ]+\n((?:\|[^\n]*\n)+)", T["sm"], re.M):
        header = split_row(re.search(r"^\|[^\n]*\bReq\b[^\n]*", T["sm"][blk.start():], re.M).group(0))
        ri = header.index("Req"); ti = header.index("TC") if "TC" in header else None
        for row in blk.group(1).splitlines():
            c = split_row(row); 
            if len(c) <= ri: continue
            rid = c[0]; nrows += 1
            for item in re.split(r",\s*", c[ri]):
                item = item.strip()
                if not item: errs.append("transition %s has empty Req" % rid)
                elif re.match(r"(FR|NFR)-\d+$", item) and item not in srs: errs.append("transition %s cites unknown %s" % (rid, item))
                elif re.match(r"D-\d+$", item) and item not in decisions: errs.append("transition %s cites unknown %s" % (rid, item))
                elif not re.match(r"((FR|NFR)-\d+|D-\d+|spec [\d.]+)$", item): errs.append("transition %s has odd Req '%s'" % (rid, item))
            if ti is not None and len(c) > ti:
                for t in re.findall(r"TC-\d+", c[ti]):
                    if t not in tcs: errs.append("transition %s cites unknown %s" % (rid, t))
                if not re.findall(r"TC-\d+", c[ti]): errs.append("transition %s has no TC" % rid)
    if nrows == 0: errs.append("no state-machine transition rows found")
    # pending markers and old names
    for k, txt in T.items():
        for pat, what in ((r"\bPENDING\b|\[pending[^\]]*\]|\(pending [A-Z]+-\d+[^)]*\)|\bpending (?:F|V12)-\d+", "pending marker"),
                          (r"(?:SRS|traceability)[_ ]v?1\.[01]\b|protocol[_ ]spec[_ ]v?1\.0\b", "old document name")):
            for m in re.finditer(pat, txt, re.I if what == "old document name" else 0):
                errs.append("%s: %s '%s'" % (DOCS[k], what, m.group(0)))
    # sprint summary
    if a.sprint:
        assigned = [m.group(1) for m in re.finditer(r"^\|\s*(TC-\d+)\s*\|\s*%s\s*\|" % a.sprint, T["plan"], re.M)]
        sp = os.path.join(a.test_logs, "summary.csv")
        if not os.path.exists(sp): errs.append("%s missing" % sp); rows = []
        else: rows = list(csv.DictReader(open(sp, newline="")))
        for t in assigned:
            hit = [r for r in rows if r.get("TC") == t and r.get("sprint") == a.sprint]
            if not hit: errs.append("summary.csv has no %s row for %s" % (a.sprint, t))
            elif any(r.get("result") not in ("PASS", "FAIL", "BLOCKED", "N/A") for r in hit): errs.append("summary.csv bad result for " + t)
        notes.append("sprint %s TCs: %s" % (a.sprint, ", ".join(assigned)))
    n_req = len(srs); n_ok = len([r for r in srs if trace.get(r)])
    print("requirements=%d with_test=%d (%.0f%%) test_cases=%d transitions=%d" % (n_req, n_ok, 100.0 * n_ok / max(n_req, 1), len(tcs), nrows))
    for n in notes: print(n)
    for e in errs: print("FINDING", e)
    print("RESULT", "PASS" if not errs else "FAIL (%d)" % len(errs))
    return 1 if errs else 0

if __name__ == "__main__": sys.exit(main())
