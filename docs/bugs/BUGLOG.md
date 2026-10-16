# Bug log (docs/bugs/BUGLOG.md)

| ID | Title | Sev | Status | Found in (TC, sprint) | Req | Fix commit | Regression test | Closed |
|---|---|---|---|---|---|---|---|---|
| BUG-001 | `tools/s1_run.py` reported TC-12 FAIL: tool-test discovery failed (start directory not importable) | P3 | Closed | TC-12, S1 (authoring dry run) | FR-12 (evidence tooling only) | in the S1 commit | `tools/s1_run.py` TC-12 now requires the tool tests to run and pass; discovery runs in CI | S1 |

No product-code defect (P1/P2) was found in S1. Spec and test-plan defects are not bugs; they are listed in `docs/s1_conflict_report.md`
for the owner to add to `docs/s0_findings_v1.md` section 4.
