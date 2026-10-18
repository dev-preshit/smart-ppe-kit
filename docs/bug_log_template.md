# Bug Log Template (v1)

| Item | Value |
|---|---|
| Document | `docs/bug_log_template.md` (document revision 1.1) |
| Used with | `docs/test_plan_v1.md` (sections 5.4 and 5.5), `docs/SRS_v1.2.md` |
| Files | `docs/bugs/BUGLOG.md` (index), `docs/bugs/BUG-###.md` (one file per bug, copy of section 3) |

### Revision history

| Revision | Change |
|---|---|
| 1.0 | First S0 draft. |
| 1.1 | Aligned with SRS 1.2: new P1 examples (`NODE_FAULT` and `KEY_FAULT` cases), and a defect in an approved document is added to `docs/s0_findings_v1.md` section 4. |

## 1. Severity

| Level | Meaning | Examples | Rule |
|---|---|---|---|
| **P1** | Safety, security or fail-safe violation, or an alert that can be lost | Motor on continuously or longer than 400 ms. Forged or replayed packet accepted. Counter repeats or decreases. A NOT_WORN or OFFLINE node does not start the alert. A node transmits without a persisted counter block or after `NODE_FAULT`. A hub with no key accepts a packet or does not run the `KEY_FAULT` pattern. Real key in a commit. Hub or node reset loop | Stop. Fix before any other work in the sprint. Sprint cannot close. No merge to `develop` |
| **P2** | A requirement or acceptance criterion is not met, no safety or security impact | Grace timing outside ±0.5 s. Reception below 99%. Wrong flag value. Message missing a field. Buffer drops wrongly | Fix before sprint close, or the owner signs a written waiver with the target sprint |
| **P3** | Minor, cosmetic or documentation, no requirement affected | Log wording. Comment or doc error. Harmless extra serial output | Fix when convenient, listed at sprint close |

A defect in an approved document (conflict, gap) is not a bug. It is added to `docs/s0_findings_v1.md` section 4 (next id after V12-09) for the owner to decide, and the fix does not work around it.

## 2. Index (`docs/bugs/BUGLOG.md`)

| ID | Title | Sev | Status | Found in (TC, sprint) | Req | Fix commit | Regression test | Closed |
|---|---|---|---|---|---|---|---|---|
| BUG-001 | | P1/P2/P3 | Open | | | | | |

Status values: `Open`, `In progress`, `Fixed` (fix merged, not yet retested), `Closed` (retest PASS, regression test in place), `Waived` (P2 only, owner signed), `Duplicate`, `Not a bug`.

## 3. Bug record (copy to `docs/bugs/BUG-###.md`)

```markdown
# BUG-### <short title>

| Field | Value |
|---|---|
| Severity | P1 / P2 / P3 |
| Status | Open |
| Reported by / date | |
| Found in | TC-xx, sprint Sx, `test_logs/<sprint>/<TC>/<run>/` |
| Requirement(s) | FR-xx, NFR-xx |
| State machine transition(s) | N-xx / H-xx / ... |
| Finding link (if related) | F-nn, V12-nn or none |

## Environment
- Firmware: node `<sha7>`, hub `<sha7>`, tools `<sha7>`
- Toolchain: Arduino IDE 2.3.10, esp32 core 3.0.7, NimBLE-Arduino 2.5.1 (note any difference)
- Devices: board labels and roles (N1 to N5, H1), supply, Wi-Fi AP, gateway build
- Parameters changed from default: 
- Test hooks in the build: yes / no

## Steps to reproduce
1. 
2. 
3. 

Reproducibility: always / n of m trials / once

## Expected (SRS acceptance criterion)

## Actual

## Evidence
- Logs, captures, analysis output (paths in `test_logs/`)
- Measured values against the pass rule

## Analysis
- Root cause:
- Why the host tests did not catch it:
- Affected requirements or other TCs:

## Fix
- Branch / commit:
- Description:
- Approved requirement change needed? No / Yes (then raise a finding, do not change the SRS here)

## Regression
- Test added (host test name or TC step):
- Fails before the fix: yes / no
- Passes after the fix: yes / no
- Regression tier run (R1 / R2 / R3) and result:

## Retest
- TC re-run on build `<sha7>`: PASS / FAIL, run folder:
- Closed by / date:
```

## 4. Rules

1. One bug per file. Do not close a bug without a retest `result.json` that links it.
2. P1 and P2 need a regression test before closing (test plan section 5.5).
3. A P2 waiver names the owner, the reason and the sprint in which it must be fixed.
4. A bug found while running a TC sets that run's `result` to FAIL and lists the bug id in `result.json`.
5. Hardware faults (a bad board, a loose wire) are logged as `Not a bug` with the cause, so the run history stays complete.
