# Traceability: Requirement to Test (v1.2)

| Item | Value |
|---|---|
| Document | `docs/traceability_v1.2.md` |
| Status | APPROVED (S0 closed) (resolves S0 findings F-01 to F-20) |
| Covers | `docs/SRS_v1.2.md` (46 functional + 13 non-functional = 59 requirements, 58 test cases) |

Test IDs name the test cases. Their steps, tooling and pass rules are detailed in `docs/test_plan_v1.md`. The acceptance criterion in the SRS is the pass rule. New in v1.2: TC-17 and TC-46.

**Levels:** U = host/unit, B = bench/hardware, S = system (hub + nodes + uplink), R = review/inspection.
**Methods:** T = test, I = inspection, A = analysis, D = demonstration.

## 1. Functional requirements

### 1.1 Node

| Req | Title | Test | Level | Method | Test summary |
|---|---|---|---|---|---|
| FR-01 | Identity from NVS, same binary | TC-01 | B | T | One binary, two boards, different provisioning. Unprovisioned board silent |
| FR-02 | Glove sensing | TC-02 | B | T | 20 contact cycles per glove |
| FR-03 | FSR hysteresis | TC-03 | U/B | T | Injected count sequence, dead-band power-on |
| FR-04 | Helmet two-factor | TC-04 | B | T | 4-combination truth table |
| FR-05 | Settle time | TC-05 | B | T | `SETTLE_MS` 0 and 500 with short and long pulses |
| FR-06 | Grace period | TC-06 | B | T | Scenarios (a) to (e), timing by logic analyzer, change during a running sequence |
| FR-07 | Event transmission | TC-07 | B | T | 20 wear + 20 remove cycles, sniffer |
| FR-08 | Boot behavior | TC-08 | B | T | Power-on worn / not worn, first packet at 25 s with `BOOT=1`, candidate change during the wait restarts the timer |
| FR-09 | Heartbeat | TC-09 | B | T | 10 intervals timed, restart after event, heartbeat due during grace (`HEARTBEAT_S=60`), RTC clock accuracy |
| FR-10 | 5-copy retransmission | TC-10 | B | T | Sniffer capture of copies, spacing, no 6th copy |
| FR-11 | Counter + NVS block + deep-sleep wake | TC-11 | B | T | Increment by 1, 100 forced resets, NVS write log across 20 wakes, `BOOT=1` only after a reset |
| FR-12 | HMAC generation | TC-12 | U/B | T | Appendix A vectors, Python verifier on 1000 packets |
| FR-13 | Battery report | TC-13 | B | T | Bench supply sweep, ADC failure |
| FR-14 | Deep sleep | TC-14 | B | T | Current profile, awake time per event |
| FR-15 | Wake level flip + loop guard | TC-15 | B | T | 20 cycles wake from deep sleep, dead-band FSR and half-worn helmet switch to timer-poll after 2 wakes |
| FR-16 | Sensor fault (FSR nodes) | TC-16 | B | T | Forced ADC failure on boots, fault set and cleared sends an EVENT. Gloves and helmet N/A (D-15) |
| FR-17 | Node faults (`NODE_FAULT`) | TC-17 | B | T | Forced NVS block write failure, counter limit, BLE start failure: no packet, hub OFFLINE, alert, no repeated counter |

### 1.2 Hub

| Req | Title | Test | Level | Method | Test summary |
|---|---|---|---|---|---|
| FR-20 | Continuous scan | TC-20 | S | T | 5 nodes received, beacon every 10 s for 1 h |
| FR-21 | Scan watchdog | TC-21 | B | T | Forced stall with test hook, 3 forced failures, 10 min with no BLE traffic and no restart |
| FR-22 | Receive filter order | TC-22 | U/B | T | Crafted packets, one defect each (including valid HMAC with invalid `state`), drop counters |
| FR-23 | HMAC verify | TC-23 | U/B | T | Valid vector, 136 bit-flip variants |
| FR-24 | Counter rule + persistence | TC-24 | U/B | T | Duplicates, replays, replay after hub reset (reset at least 2 s after the last copy) |
| FR-25 | Registry | TC-25 | B | T | Reset hub, check retained counters, states UNKNOWN |
| FR-26 | Node state machine | TC-26 | U | T | Scripted transitions per spec 8.2 |
| FR-27 | Offline detection | TC-27 | S | T | Power-off timing 780 to 785 s, margin 0 case, one dropped heartbeat |
| FR-28 | Compliance engine | TC-28 | U | T | Truth table, per node NOT_WORN / OFFLINE / UNKNOWN, one `COMPLIANCE` message per vest change, none at boot |
| FR-29 | Motor alert | TC-29 | B | T | Pattern timing, start/stop latency, UNKNOWN silent |
| FR-30 | Hub fault handling | TC-30 | B | T | Fault injection with codes, missing key (`NO_KEY` within 2 s, `KEY_FAULT` pattern timing), unreadable registry |
| FR-31 | Hub boot behavior | TC-31 | S | T | Reset, UNKNOWN, `HUB_BOOT`, offline from boot |
| FR-32 | Diagnostic counters | TC-32 | U/B | T | Crafted-packet run vs global and per-node counters, `drop_state` |

### 1.3 Provisioning

| Req | Title | Test | Level | Method | Test summary |
|---|---|---|---|---|---|
| FR-40 | Node provisioning | TC-40 | B | T | Persist across power cycles, out-of-range rejected |
| FR-41 | Hub provisioning | TC-41 | B | T | Persist, out-of-range rejected, `HEARTBEAT_S` drives the offline timeout, only node ids 1 to 5 accepted |
| FR-42 | Key handling | TC-42 | R | I/T | Secret scan over all commits, build without key in source |
| FR-43 | `reset-counter` command | TC-43 | S | T | Replaced node dropped, then accepted after reset |
| FR-44 | Configurable timeouts | TC-44 | B | T | Changed values measured |
| FR-45 | Serial-only provisioning | TC-45 | S | T/I | Radio scan, no config service, no packet changes config |
| FR-46 | `factory-reset` command | TC-46 | S | T | Node silent and hub in `NO_KEY` after reset, re-provision restores, no `CONFIRM` changes nothing |

### 1.4 Uplink

| Req | Title | Test | Level | Method | Test summary |
|---|---|---|---|---|---|
| FR-50 | Forwarding | TC-50 | S | T | 50 mixed events once each at the gateway, retried POST stored once |
| FR-51 | Message format | TC-51 | U/S | T | Schema validation including `detail`, `hub_seq` gap-free within a boot |
| FR-52 | Buffered retry | TC-52 | S | T | 30 min outage in the controlled setup, order, oldest dropped, missing `hub_seq` = `dropped` + `superseded` |
| FR-53 | Wi-Fi non-blocking | TC-53 | S | T | Failing association, timings unchanged |
| FR-54 | `HUB_STATUS` | TC-54 | S | T | 10 intervals at 60 ± 3 s |

### 1.5 Dashboard

| Req | Title | Test | Level | Method | Test summary |
|---|---|---|---|---|---|
| FR-60 | Live status | TC-60 | S | T | Injected data for 5 nodes, update ≤ 5 s |
| FR-61 | Arrival timestamp and `age_ms` | TC-61 | S | T | Delayed buffered event, ±1 s |
| FR-62 | Event history | TC-62 | S | T | 1000 events, order, compliance-restored entry |
| FR-63 | Dashboard alerts | TC-63 | S | T | Each condition raised and cleared |

## 2. Non-functional requirements

| Req | Title | Test | Level | Method | Test summary |
|---|---|---|---|---|---|
| NFR-01 | Alert latency ≤ 28 s | TC-70 | S | T | 30 remove trials per node type |
| NFR-02 | Reception reliability | TC-71 | S | T | 100 events per node at ≤ 2 m |
| NFR-03 | Hub processing time | TC-72 | B | T | Timestamp instrumentation, burst of 25 |
| NFR-04 | Security | TC-73, TC-42 | S/R | T/I | 1000 forged, 100 replays, secret scan |
| NFR-05 | Motor safety | TC-74 | B | T | 1 h with injected faults, longest ON ≤ 400 ms, low at boot, guard trip reported |
| NFR-06 | Counter durability | TC-75, TC-11 | B | T | 1000 random power cuts, strictly increasing log |
| NFR-07 | Uplink latency | TC-76 | S | T | 50 events within 5 s |
| NFR-08 | Stability | TC-77 | S | T | 24 h soak, resets and heap |
| NFR-09 | Toolchain | TC-78 | R | I/T | Clean build with pinned versions |
| NFR-10 | Packet size | TC-79 | R/B | I/T | 21 bytes, confirmed on air |
| NFR-11 | Battery (informational) | TC-80 | B | A | Current measured, life estimated, report only |
| NFR-12 | Traceability | TC-81 | R | I | Script checks every requirement ID has a test row |
| NFR-13 | Fail-safe default | TC-03, TC-16, TC-28, TC-30 | U/B | T | Covered by the linked tests |

## 3. Coverage check

| Group | Requirements | With a test |
|---|---|---|
| Node FR-01 to FR-17 | 17 | 17 |
| Hub FR-20 to FR-32 | 13 | 13 |
| Provisioning FR-40 to FR-46 | 7 | 7 |
| Uplink FR-50 to FR-54 | 5 | 5 |
| Dashboard FR-60 to FR-63 | 4 | 4 |
| Non-functional NFR-01 to NFR-13 | 13 | 13 |
| **Total** | **59** | **59** |

## 4. Reverse index (tests shared by more than one requirement)

| Test | Requirements |
|---|---|
| TC-03 | FR-03, NFR-13 |
| TC-11 | FR-11, NFR-06 |
| TC-16 | FR-16, NFR-13 |
| TC-28 | FR-28, NFR-13 |
| TC-30 | FR-30, NFR-13 |
| TC-42 | FR-42, NFR-04 |
