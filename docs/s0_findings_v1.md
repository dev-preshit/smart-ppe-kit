# S0 Review Findings: Conflicts and Gaps in the Approved Documents (v1)

| Item | Value |
|---|---|
| Document | `docs/s0_findings_v1.md` |
| Reviewed | The S0 baseline (tag `s0-docs-done`): SRS, protocol spec and traceability before revision. Resolved in `docs/SRS_v1.2.md`, `docs/protocol_spec_v1.1.md`, `docs/traceability_v1.2.md` |
| Status | **RESOLVED**. All findings F-01 to F-20 approved by the owner and applied in SRS 1.2, protocol spec 1.1 and traceability 1.2. Section 4 lists new observations, not decided. |
| Rule followed | The reviewer changed nothing in the approved documents. Each finding had a proposed resolution, the owner approved all of them (with the specifics recorded below) and applied them in the revised documents. |

### Revision history

| Revision | Change |
|---|---|
| 1 | S0 review: 20 findings with proposals, decisions open. |
| 1.1 | All findings approved and resolved. Added the resolution column, the decision notes in section 2, section 3 as applied changes, and section 4 (observations V12-01 to V12-09 on SRS 1.2, not decided). |

**Types:** C = conflict (documents contradict each other or themselves), G = gap (something needed is missing), A = ambiguity (more than one reading), O = observation (no action needed, recorded for sign-off).
**Severity:** H = blocks a state machine or test, M = needs a decision before the sprint that builds it, L = wording or minor.
**Decision column:** APPROVE for all 20 (owner). Specific answers for F-01, F-03, F-05, F-16, F-18 and F-20 are in the details.

## 1. Summary

| ID | Type | Sev | Where | One-line issue | Blocks | Decision | Resolved in |
|---|---|---|---|---|---|---|---|
| F-01 | C | H | FR-52 vs FR-50, FR-54 | Retry-buffer AC ignores heartbeat and `HUB_STATUS` traffic, so the AC numbers cannot be met | TC-52 (S7) | APPROVE | FR-52, protocol spec 10.2, TC-52 |
| F-02 | G | H | FR-11, FR-08, spec 6.1.4, 3.1 | "Boot" is not defined against deep-sleep wake. Read literally, every wake advances the counter block and sets `BOOT=1` | N-02, N-03, TC-08, TC-11 (S4) | APPROVE | D-16, FR-08, FR-11, spec 1, 6.1.4 |
| F-03 | G | H | FR-21, AS-08 | "No progress for 10 s" is undefined. Nodes are silent for up to 360 s, so a packet-based watchdog would reboot the hub every ~40 s | S-02, S-03, TC-21 (S3) | APPROVE | FR-21, spec 9.3, TC-21 |
| F-04 | C | M | FR-16 vs FR-06 | A fault set or cleared with an unchanged state sends nothing under FR-06 | N-21, TC-16 (S2) | APPROVE | FR-16, spec 8.1 |
| F-05 | G | M | FR-16 | "Invalid sensor reading" is not defined per sensor. A disconnected glove or helmet contact looks like "open" | R-4, TC-16 (S2) | APPROVE | D-15, R-06, FR-16, TC-16 |
| F-06 | G | M | FR-41 vs FR-27, FR-44 | Hub provisioning has no `HEARTBEAT_S`, but `OFFLINE_TIMEOUT_S` is derived from it | TC-27, TC-44 (S5, S6) | APPROVE | FR-41, TC-41 |
| F-07 | C | L | FR-27 AC | The 720 s lower bound only holds for margin 0. At the default (780 s) OFFLINE cannot occur before 780 s | TC-27 (S5) | APPROVE | FR-27 |
| F-08 | G | M | spec 10, FR-51, FR-52, FR-54 | The uplink schema has no fields or values for `COMPLIANCE`, `HUB_STATUS`, `HUB_FAULT`, dropped count | TC-51 (S7) | APPROVE | spec 10, 10.1, FR-51 |
| F-09 | A | L | spec 10, FR-50, FR-51 | `hub_seq` across hub reboots and gateway de-duplication are not defined | TC-50, TC-51 (S7) | APPROVE | D-17, FR-50, spec 10.2 |
| F-10 | A | M | D-04, SRS 1.5 vs FR-26, spec 8.2 | "UNKNOWN at power-on and reconnection" vs "UNKNOWN only at hub boot" | H-01..H-13 (S5) | APPROVE | D-04, SRS 1.5 |
| F-11 | G | M | FR-09, spec 4, 7.5 | Heartbeat due during grace, and grace expiry during a sequence, are not defined | N-26, N-28, TC-09 (S4) | APPROVE | FR-06, FR-09, spec 4, 7.5 |
| F-12 | G | M | FR-15, FR-03, AS-04 | Level-triggered wake can loop for FSR dead band and for helmet AND logic | N-10, TC-15 (S2, S4) | APPROVE | FR-15, spec 8.1.7 |
| F-13 | A | L | FR-08, FR-06, D-03 | Boot grace when the candidate changes during the 25 s | N-23, TC-08 | APPROVE | FR-08, spec 8.1.5 |
| F-14 | A | L | spec 9.3, 3, FR-22, FR-32 | Some "per node" counters cannot be attributed. Drop point for invalid `state` is missing | TC-22, TC-32 (S3) | APPROVE | FR-22, FR-32, spec 2, 9.3 |
| F-15 | A | L | FR-41, spec 3.2, FR-30 | "5 registered `node_id`s" vs fixed ids 1 to 5, and the unreadable-registry case | B-04, TC-41 | APPROVE | FR-25, FR-41, spec 8.2 |
| F-16 | G | L | SRS section 8, feature list | Two items from the approved feature list have no FR: hub event history ring, provisioning factory reset | traceability | APPROVE | D-19, FR-46, TC-46 |
| F-17 | G | M | FR-28, spec 8.2, 10 | "Every transition reported" is undefined: node or vest level, initial state, direction | C-01..C-04, TC-28 (S5) | APPROVE | FR-28, spec 10.1 |
| F-18 | G | M | FR-30, spec 6.1.5 | Node-side faults (NVS write failure, BLE start failure) have no defined behaviour | N-09, N-30 (S4) | APPROVE | FR-17, spec 6.1.5, TC-17 |
| F-19 | O | L | FR-52, FR-53, FR-30, NFR-05 | Unspecified design values: backoff schedule, HTTP timeout, guard trip action, NVS write failure on hub | U-04, M-05 | APPROVE | AS-16, AS-18, NFR-05, FR-30 |
| F-20 | O | L | FR-06, AS-15, FR-27 | Timing risks: deep-sleep wake latency vs 25.0 ± 0.5 s, RTC clock error vs margin bound, persist-lag window | TC-06, TC-27, TC-24 | APPROVE | FR-30, D-18, AS-15, AS-17, AS-18 |

## 2. Details

### F-01 (C, H): FR-52 acceptance criterion vs FR-50 and FR-54 traffic

**Decision: APPROVED.** Approved as proposed. `HUB_STATUS` is coalesced in the buffer and counted as superseded. Missing `hub_seq` values equal `dropped` + `superseded` (FR-52, protocol spec 10.2). TC-52 uses the controlled setup (nodes at `HEARTBEAT_S=3600`) and is no longer blocked.

FR-50 forwards every accepted packet (heartbeats included) and FR-54 sends `HUB_STATUS` every 60 s. Both share the 100-message buffer.

In the 30 min outage of the FR-52 AC (defaults):
- 30 `HUB_STATUS` messages (1800 s / 60 s)
- 25 heartbeat `PACKET` messages (5 nodes × 5 heartbeats at 360 s)
- plus the 60 test events

That is 115 messages, so 15 of the oldest are dropped. The AC says "all 60 delivered". With 120 events the total is 175, so 75 are dropped, not "the oldest 20, `dropped=20`".

**Proposal (needs approval):**
1. Coalesce `HUB_STATUS` in the buffer: at most one entry, the newest replaces the older. It is a snapshot, not an event. (Heartbeat `PACKET` messages are still queued.)
2. Reword the AC so it holds regardless of background traffic: "after restore, every delivered message is in `hub_seq` order, the messages missing are exactly the oldest ones, and the reported `dropped` equals the number of missing `hub_seq` values."
3. Run TC-52 in a controlled setup (nodes at `HEARTBEAT_S=3600`, outage 30 min). With the proposal: 60 events + 1 `HUB_STATUS` + about 5 heartbeats fits in 100. With 120 events the expected `dropped` is computed from the sent count (about 26), not fixed at 20.

Until decided, TC-52 is `BLOCKED` on its numeric pass rule.

### F-02 (G, H): Deep-sleep wake is not defined as "boot"

**Decision: APPROVED.** Approved as proposed (D-16, FR-08, FR-11, protocol spec 1 and 6.1.4).

On the ESP32-C3, each deep-sleep wake restarts the program and RAM is lost (RTC memory survives). Spec 6.1.4 says "on boot (power-on, reset, brown-out)" `counter = ceiling` and `ceiling` advances by 64 with an NVS write. FR-08 and `BOOT=1` are "after power-on".

If every wake counted as a boot: every heartbeat and event would write NVS (FR-11 AC says at most 1 write per 64 packets), every packet would carry `BOOT=1`, and `confirmed` would be `NONE` after each sleep (no event could ever be detected as a change).

**Proposal (interpretation, no text change needed if acknowledged):**
- A boot is any start whose reset reason is not "deep-sleep wake", or a deep-sleep wake whose RTC context fails its magic and CRC check.
- Deep-sleep wake restores `confirmed`, `counter`, `ceiling`, `hb_deadline`, `sensor_fault` from RTC memory.
- RTC context is trusted only for reset reason = deep-sleep wake. Anything else uses the cold-boot path (safe: `BOOT=1`, counter block advance).

### F-03 (G, H): Scan watchdog "progress"

**Decision: APPROVED.** Approved, with an addition: the log-only counter `scan_quiet_60s` (protocol spec 9.3). FR-21 now polls every 1 s, receiving no advertisements is not a stall, and the AC adds 10 min with no BLE traffic.

FR-21: "if scanning makes no progress for 10 s ... restart". Nodes send at most once per 360 s, and a quiet environment may have no BLE traffic at all. "Progress = any advertisement received" would trigger a restart every 10 s and a hub reboot after 3 failed restarts (about 40 s). The FR-20 test (beacon every 10 s) hides this.

**Proposal:** progress = the scanner reports active (host stack query polled every 1 s plus scan-start and scan-end callbacks). A stall is "scanner not active, or host reports a sync loss, for 10 s". A forced stall for TC-21 uses a test hook (`PPE_TEST_HOOKS`). Optional later: hub self-check using a known beacon.

### F-04 (C, M): FR-16 vs FR-06

**Decision: APPROVED.** Approved as proposed (FR-16, protocol spec 8.1).

FR-06 sends nothing when the candidate equals the confirmed state. If a node is already confirmed NOT_WORN and the sensor becomes invalid, the reported state is still NOT_WORN, so no event is sent until the next heartbeat (up to 360 s). The FR-16 AC expects a packet after 25 ± 0.5 s.

**Proposal:** the monitored value is the pair (`state`, `sensor_fault`). A change in either starts grace and produces an EVENT after `GRACE_S`. This includes the fault clearing.

### F-05 (G, M): "Invalid sensor reading"

**Decision: APPROVED.** Approved, with the answer: gloves and helmet have no sensor-fault detection in v1 (D-15, R-06). A disconnected contact reads NOT_WORN and raises the alert. TC-16 applies to FSR boots only.

Not defined per sensor type. Glove and helmet contacts are digital: a disconnected wire is electrically the same as an open contact (NOT_WORN), so a fault cannot be detected without extra hardware. An FSR with the sensing leg open reads near 0, which is also a valid NOT_WORN.

**Proposal:** `SENSOR_FAULT` = ADC read returns an error or times out, or (FSR) the count is outside the calibrated plausible range. Digital contacts: no fault detection unless the S2 hardware adds it. TC-16 then uses the forced ADC failure for FSR nodes and is marked N/A for gloves and helmet unless hardware changes. Owner to confirm.

### F-06 (G, M): `HEARTBEAT_S` not provisioned on the hub

**Decision: APPROVED.** Approved as proposed (FR-41 adds `HEARTBEAT_S`, margin recommended at least 40 s).

`OFFLINE_TIMEOUT_S = 2 × HEARTBEAT_S + OFFLINE_MARGIN_S` (spec 1.1). FR-41 does not list `HEARTBEAT_S` for the hub. If a node is provisioned with `HEARTBEAT_S=60` (FR-44 AC) the hub still assumes 360 s.

Related: the margin bound allows 0 to 300 s. With the allowed ±5% timer error (AS-15), a margin below about 40 s can declare OFFLINE after one lost heartbeat (2 × 378 s = 756 s, plus sequence time, vs 720 s at margin 0).

**Proposal:** add `HEARTBEAT_S` to FR-41 (one value for the whole vest). Document a recommended minimum margin of 40 s outside tests.

### F-07 (C, L): FR-27 acceptance window

**Decision: APPROVED.** Approved as proposed (FR-27 window 780 s to 785 s).

AC: "OFFLINE between 720 s and 785 s". With the default 780 s the earliest OFFLINE is just after 780 s. The 720 value belongs to margin 0 (second AC line).

**Proposal:** write the first line as "between 780 s and 785 s at default margin".

### F-08 (G, M): Uplink schema is incomplete

**Decision: APPROVED.** Approved as proposed (protocol spec 10 and 10.1, schema in S1).

Spec section 10 lists common fields only.
- `COMPLIANCE`: no field or value carries COMPLIANT / NON_COMPLIANT (the `state` enum is WORN, NOT_WORN, OFFLINE, UNKNOWN).
- `HUB_STATUS` (FR-54): uptime, per-node state, diagnostics, buffer fill have no fields.
- `HUB_FAULT`: no fault code. FR-52: no field for the dropped count.
- `COUNTER_ANOMALY`, `COUNTER_RESET`: no detail fields.

FR-51's AC (validate against the schema) needs a complete schema.

**Proposal:** add an additive `detail` object per `msg_type`, and add `COMPLIANT` and `NON_COMPLIANT` as `state` values for `COMPLIANCE`. Published as `tools/schema/uplink.schema.json` in S1 and referenced from the spec in a minor revision.

### F-09 (A, L): `hub_seq` across reboots

**Decision: APPROVED.** Approved as proposed (D-17, FR-50, protocol spec 10.2).

`hub_seq` is not persisted (no clock, no per-message NVS write). **Proposal:** `hub_seq` starts at 0 at each hub boot. `HUB_BOOT` is the first message with `hub_seq=0`. Gap-free applies within a boot. The gateway de-duplicates retried POSTs on (boot epoch, `hub_seq`), where the epoch increments on each `HUB_BOOT`. This is what makes "exactly once" (FR-50) hold when a response is lost.

### F-10 (A, M): UNKNOWN and "reconnection"

**Decision: APPROVED.** Approved as proposed (D-04 and SRS 1.5 reworded).

SRS 1.5 and D-04: UNKNOWN "only occurs at power-on and reconnection". FR-26 and spec 8.2: UNKNOWN only at hub boot. A node reconnecting goes OFFLINE to WORN or NOT_WORN, and a node reset keeps its previous state on the dashboard until its first `BOOT=1` packet.

**Proposal:** the state machines follow FR-26 and spec 8.2. Reword D-04 to "UNKNOWN occurs only after hub power-on or reset".

### F-11 (G, M): Heartbeat during grace, grace expiry during a sequence

**Decision: APPROVED.** Approved as proposed (FR-06, FR-09, protocol spec 4 and 7.5).

Spec 4 covers a heartbeat due while a sequence is in progress. It does not cover a heartbeat due while the node is in grace (node awake, no sequence). Deferring until grace ends could give an interval above 378 s (FR-09 AC). Spec 7.5 says a change during a sequence is handled "after the sequence ends", but not whether the grace timer starts at the change or at sequence end.

**Proposal:**
- A heartbeat due during grace is sent immediately with the confirmed state. Grace continues.
- The grace timer runs from the candidate change. If it expires while a sequence is in progress, the EVENT starts when the sequence ends. This only matters for `GRACE_S` below 3 s.

### F-12 (G, M): Level-triggered wake can loop

**Decision: APPROVED.** Approved as proposed (FR-15, protocol spec 8.1.7).

FR-15 arms a level-triggered wake at the level opposite the confirmed state. That is natural for gloves and for removal. Two cases loop:
- **Boots (FSR):** the wake threshold in hardware (comparator) and the firmware hysteresis (1537 / 88) differ. After a wake with a count in the dead band, the firmware holds the state, but the wake level is still asserted and the node wakes again immediately. AS-04 already lists the wake mechanism as a design item.
- **Helmet:** wearing requires crown AND strap. A GPIO wake fires on a single closed contact, so a half-worn helmet causes repeated wakes until the second contact closes.

**Proposal:** a loop guard in the node state machine. After K=2 consecutive GPIO wakes with no candidate change, arm timer-poll wake (period set in S2, ≤ 1 s so the NFR-01 3 s budget still holds) until the reading leaves the condition. Hardware choice (comparator with matching thresholds, AND gate, or polling) stays an S2 decision.

### F-13 (A, L): Boot grace stability

**Decision: APPROVED.** Approved as proposed (FR-08).

With `confirmed = NONE` every candidate differs. Spec 8.1.5 does not say what happens if the candidate changes during the 25 s. **Proposal:** the timer restarts (the state must be stable for `GRACE_S` before the `BOOT=1` EVENT), consistent with D-03. `SETTLE_MS` is not applied to the first reading.

### F-14 (A, L): Diagnostic counters

**Decision: APPROVED.** Approved as proposed (FR-22, FR-32, protocol spec 2 and 9.3).

Spec 9.3 says counters are per node. `drop_len`, `drop_ver`, `drop_vest`, `drop_node` happen before `node_id` is trusted and `rx_total` is global. Also, a packet with a valid HMAC and an invalid `state` (spec section 3: "drop") has no counter.

**Proposal:** global: `rx_total`, `drop_len`, `drop_ver`, `drop_vest`, `drop_node`. Per node: `hmac_fail`, `dup_count`, `replay_count`, `gap_count`, `accepted`. Add `drop_state` (global) for the invalid-state case, checked after HMAC and before the counter rule.

### F-15 (A, L): Registered node ids

**Decision: APPROVED.** Approved as proposed (FR-25, FR-41, protocol spec 8.2).

Spec 3.2 fixes the ids 0x01 to 0x05. FR-41 provisions "the 5 registered `node_id`s". **Proposal:** the registry always has slots for 0x01 to 0x05 (compile-time list). Provisioning stores the key and counters. If the registry cannot be read, the slots still exist, so they can go OFFLINE and start the alert (FR-30).

### F-16 (G, L): Features with no FR

**Decision: APPROVED.** Approved, with the answer: the hub event history ring is dropped from v1 (D-19). A `factory-reset` serial command is added instead (FR-46, TC-46).

The approved S0 feature list additions included a hub event history ring and a provisioning factory reset. SRS section 8 does not list either as removed, and no FR or TC covers them. **Action:** confirm they are dropped from v1, or add FRs (and TCs) in a revision.

### F-17 (G, M): "Every transition reported" (FR-28)

**Decision: APPROVED.** Approved as proposed (FR-28, protocol spec 10.1).

Undefined: node-level or vest-level transitions, whether the initial state at hub boot is reported, and whether both directions are reported.

**Proposal:**
- A `COMPLIANCE` message is sent on every change of the vest state (COMPLIANT to NON_COMPLIANT and back).
- The initial NON_COMPLIANT at hub boot is not a transition. The dashboard derives it from `HUB_BOOT` and `HUB_STATUS`.
- Node-level transitions are visible through `PACKET` and `NODE_OFFLINE`.
- The dashboard's "compliance-restored" event (FR-62) is the NON_COMPLIANT to COMPLIANT message.

### F-18 (G, M): Node-side faults

**Decision: APPROVED.** Approved, as a new requirement: FR-17 (`NODE_FAULT`) with TC-17.

FR-30 lists hub faults only. Not defined for the node: NVS write failure for the counter block, BLE start failure, counter at the 6.1.5 limit (which says stop and report on serial).

**Proposal:** a `NODE_FAULT` state. Causes: counter limit (spec 6.1.5), NVS block write failure (never transmit without a persisted block, so a counter can never repeat), BLE start failure after one retry. The node stops transmitting and logs on serial. The hub sees OFFLINE and alerts, which is the fail-safe direction. Task or hardware watchdog resets take the cold-boot path.

### F-19 (O, L): Values chosen in the design, listed for sign-off

**Decision: APPROVED.** Approved as proposed (AS-16, AS-18, NFR-05, FR-30).

- Uplink retry: 1 s, doubling, capped at 30 s, reset on success. HTTP timeout 2 s. Wi-Fi reconnect: 5 s doubling, capped 30 s.
- A motor guard trip (output on > 400 ms) forces the output low, is logged, and reports `HUB_FAULT` (`GUARD`). The pattern resumes at the next ON edge.
- Hub NVS write failure: counted and reported as `HUB_FAULT`, the hub keeps running on RAM counters.
- After a hub reboot caused by a fault, the fault is stored in RTC memory and reported after boot, because the RAM queue is lost.
- Accepted `BOOT=1` packet produces `PACKET` then `NODE_REBOOT`.

### F-20 (O, L): Timing risks to measure

**Decision: APPROVED.** Approved, with a change to the last bullet: a missing HMAC key raises `HUB_FAULT` (`NO_KEY`) immediately and runs the `KEY_FAULT` motor pattern, 3 × (ON 100 ms / OFF 100 ms) then OFF 1700 ms, repeating. It has priority over the normal alert pattern (FR-30, D-18, AS-18). The timing items are recorded as AS-15, AS-17 and protocol spec 6.2.

- Deep-sleep wake to application start is likely a few hundred ms on the ESP32-C3. FR-06 allows ±0.5 s. The grace deadline is anchored to wake with a calibrated compensation (S2 measures it).
- The RTC slow clock may exceed ±5% without calibration (AS-15 assumes ±5%). Measure in S4 (TC-09).
- Hub persists `last_counter` after dispatch. A hub reset in that short window lets one later copy of the same packet be accepted again (duplicate dispatch, same state, harmless). TC-24 waits at least 2 s after the last copy before resetting the hub.
- A hub with a missing key behaves like a booting hub (no alert) for 780 s. This follows from D-04 and FR-30 and is recorded here only for awareness.

## 3. Changes applied in SRS 1.2, protocol spec 1.1 and traceability 1.2

| Document | Applied change | Findings | Where |
|---|---|---|---|
| SRS | FR-27 window 780 s to 785 s at the default margin | F-07 | FR-27 |
| SRS | `HEARTBEAT_S` added to hub provisioning, margin recommended at least 40 s | F-06 | FR-41 |
| SRS | `HUB_STATUS` coalescing, `superseded` count, reworded AC, controlled setup | F-01 | FR-52, spec 10.2 |
| SRS | Fault pair rule, FSR-only fault, no detection for gloves and helmet | F-04, F-05 | FR-16, D-15, R-06 |
| SRS | UNKNOWN wording | F-10 | D-04, section 1.5 |
| SRS | New FR-17 (`NODE_FAULT`) and FR-46 (`factory-reset`), event history ring dropped | F-16, F-18 | FR-17, FR-46, D-19 |
| SRS | Missing key: `NO_KEY` at once and `KEY_FAULT` pattern, fault codes, NVS write failure | F-19, F-20 | FR-30, D-18, AS-18 |
| SRS | Scan watchdog poll and no-traffic test | F-03 | FR-21 |
| Protocol spec | Boot definition, deep-sleep wake rule, heartbeat during grace, grace during a sequence, boot grace restart, wake loop guard | F-02, F-11, F-12, F-13 | 1, 4, 6.1.4, 7, 8.1 |
| Protocol spec | Global and per-node counters, `drop_state`, `scan_quiet_60s` | F-03, F-14 | 2, 9.3 |
| Protocol spec | Message `detail` objects, `COMPLIANT` / `NON_COMPLIANT`, `hub_seq` and delivery rules, fixed registry slots | F-08, F-09, F-15, F-17 | 8.2, 10, 10.1, 10.2 |
| Traceability | TC-17 and TC-46 added, coverage 59 of 59 | F-16, F-18 | traceability 1.2 |

## 4. Observations on SRS 1.2, protocol spec 1.1 and traceability 1.2 (not decided, not fixed)

Found while updating the state machines and test plan. The documents are unchanged. The state machines and test plan use the interpretation shown and say so (V12-xx).

| ID | Type | Sev | Where | Observation | Interpretation used | Proposal |
|---|---|---|---|---|---|---|
| V12-01 | A | L | FR-16 AC | "25 ± 0.5 s later" has no reference point when the fault arises while the node sleeps: the first faulty sample comes at the next wake, up to `HEARTBEAT_S` later | Measured from the first faulty sample, logged by the node | State the reference point in the AC |
| V12-02 | G | M | FR-40, FR-46 | A sleeping node cannot take serial commands. The only awake window on a provisioned node is the boot grace (`GRACE_S`, as short as 1 s) | Node awake for serial when unprovisioned, 60 s in `NODE_FAULT`, and during the boot grace. Tests power-cycle first and send inside the window | Define a serial window after a cold boot, or a held-awake input |
| V12-03 | A | M | FR-30, FR-46, protocol spec 8.2, 10.1 | (a) With an unreadable registry the text says nodes go OFFLINE, which only holds if no packet is accepted. (b) The fault code for an unreadable registry could be `REGISTRY` or `NVS_READ`. (c) "Until a key is provisioned and the hub restarts" does not say who restarts. (d) Scan, Wi-Fi and `HUB_STATUS` during `KEY_FAULT` are not stated | (a) accept disabled. (b) `REGISTRY` for the registry, `NVS_READ` for other reads. (c) automatic restart after the key command succeeds. (d) they keep running | State each in FR-30 |
| V12-04 | C | L | FR-31 AC vs FR-27 AC | FR-31 says 780 ± 5 s after boot, FR-27 says 780 s to 785 s. The offline time cannot be earlier than 780 s | Tests use 780 s to 785 s | Use 780 s to 785 s in FR-31 |
| V12-05 | A | M | FR-30 AC, protocol spec 8.3 | `KEY_FAULT` is 3 × (ON 100, OFF 100) then OFF 1700 ms with a 2.3 s period. The low time after the third ON is then 100 + 1700 = 1800 ms, but the AC says OFF 1700 ± 100 ms (1600 to 1800), so the nominal value sits on the upper limit | Follow the 2.3 s period. TC-30 measures the low after the third ON and records it | AC "1800 ± 100 ms after the third ON", or change the period to 2.2 s |
| V12-06 | G | L | FR-17, protocol spec 6.1.5 | Behavior after the cause is logged is not stated (stay awake, sleep, how the operator is told) | Awake 60 s for serial, then deep sleep with no wake source until a reset | State it in FR-17 |
| V12-07 | G | L | FR-11, protocol spec 6.1.3 and 6.1.4 | `ceiling + 64` can overflow the uint32 near the limit `0xFFFFFFF0` | Clamp `ceiling` to `0xFFFFFFF0`. The limit fault comes when `counter + 1` exceeds it | State the clamp |
| V12-08 | A | L | FR-52, protocol spec 10.2 | (a) Where the new `HUB_STATUS` goes in the buffer. (b) The `dropped` and `superseded` values in a buffered `HUB_STATUS` are those of its generation time. (c) The controlled setup also needs the hub `HEARTBEAT_S` at 3600 (FR-41), or `NODE_OFFLINE` messages enter the buffer | (a) remove the older one and append the new one at the tail. (b) TC-52 reads the counts from a `HUB_STATUS` generated after the restore. (c) hub at 3600 | Add (a) and (c) to FR-52 |
| V12-09 | O | L | FR-32, protocol spec 9.3 | `scan_quiet_60s` has no acceptance criterion (log only) | TC-21 step 4 checks it as a supplementary step with no pass rule | None needed |

No conflict was found between SRS 1.2, protocol spec 1.1 and traceability 1.2 in IDs, counts or test mapping. The traceability counts (46 functional, 13 non-functional, 58 test cases) match the SRS.
