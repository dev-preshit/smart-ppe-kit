# Bug log (docs/bugs/BUGLOG.md)

| ID | Title | Sev | Status | Found in (TC, sprint) | Req | Fix commit | Regression test | Closed |
|---|---|---|---|---|---|---|---|---|
| BUG-001 | `tools/s1_run.py` reported TC-12 FAIL: tool-test discovery failed (start directory not importable) | P3 | Closed | TC-12, S1 (authoring dry run) | FR-12 (evidence tooling only) | in the S1 commit | `tools/s1_run.py` TC-12 now requires the tool tests to run and pass; discovery runs in CI | S1 |
| BUG-002 | `PpeNodeCounter::resume()` accepts an implausible RTC image (for example zeroed) and next() can then lower the persisted ceiling | P2 | Fixed, retest pending | none failed; found in S1 review (TC-75 scope) | FR-11, FR-17, NFR-06 | pending | `test_counter.cpp`: `resume_refuses_implausible_rtc_image` (guard: `resume_accepts_every_image_the_counter_can_produce`) | |
| BUG-003 | `PpeNodeCounter::next()` dereferences a null store when the block must be extended (counter built with a null store and then `resume()`d) | P3 | Fixed, retest pending | none failed; found in S1 review (TC-17 scope) | FR-17 | pending | `test_counter.cpp`: `next_with_null_store_refuses_instead_of_crashing` | |

Product-code defects found in the S1 review are the rows above from BUG-002 on. Spec and test-plan defects are not bugs; they are listed in `docs/s1_conflict_report.md`
for the owner to add to `docs/s0_findings_v1.md` section 4.
