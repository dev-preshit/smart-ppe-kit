# Traceability: Requirement to Test (v1.1)

| Item | Value |
|---|---|
| Document | `docs/traceability_v1.1.md` |
| Status | APPROVED (S0) |
| Covers | `docs/SRS_v1.1.md` (44 functional + 13 non-functional = 57 requirements) |

Test IDs name the test cases. Their steps, tooling and pass rules are detailed in the test plan (next S0 chat). The acceptance criterion in the SRS is the pass rule.

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
| FR-06 | Grace period | TC-06 | B | T | Scenarios (a) to (d), timing by logic analyzer |
| FR-07 | Event transmission | TC-07 | B | T | 20 wear + 20 remove cycles, sniffer |
| FR-08 | Boot behavior | TC-08 | B | T | Power-on worn / not worn, first packet at 25 s with `BOOT=1` |
| FR-09 | Heartbeat | TC-09 | B | T | 10 intervals timed, restart after event |
| FR-10 | 5-copy retransmission | TC-10 | B | T | Sniffer capture of copies, spacing, no 6th copy |
| FR-11 | Counter + NVS block | TC-11 | B | T | Increment by 1, 100 forced resets, NVS write log |
| FR-12 | HMAC generation | TC-12 | U/B | T | Appendix A vectors, Python verifier on 1000 packets |
| FR-13 | Battery report | TC-13 | B | T | Bench supply sweep, ADC failure |
| FR-14 | Deep sleep | TC-14 | B | T | Current profile, awake time per event |
| FR-15 | Wake level flip | TC-15 | B | T | 20 cycles wake from deep sleep |
| FR-16 | Sensor fault | TC-16 | B | T | Disconnected sensor, ADC failure |

### 1.2 Hub

| Req | Title | Test | Level | Method | Test summary |
|---|---|---|---|---|---|
| FR-20 | Continuous scan | TC-20 | S | T | 5 nodes received, beacon every 10 s for 1 h |
| FR-21 | Scan watchdog | TC-21 | B | T | Forced stall, 3 forced failures |
| FR-22 | Receive filter order | TC-22 | U/B | T | Crafted packets, one defect each, drop counters |
| FR-23 | HMAC verify | TC-23 | U/B | T | Valid vector, 136 bit-flip variants |
| FR-24 | Counter rule + persistence | TC-24 | U/B | T | Duplicates, replays, replay after hub reset |
| FR-25 | Registry | TC-25 | B | T | Reset hub, check retained counters, states UNKNOWN |
| FR-26 | Node state machine | TC-26 | U | T | Scripted transitions per spec 8.2 |
| FR-27 | Offline detection | TC-27 | S | T | Power-off timing, margin 0, one dropped heartbeat |
| FR-28 | Compliance engine | TC-28 | U | T | Truth table, per node NOT_WORN / OFFLINE / UNKNOWN |
| FR-29 | Motor alert | TC-29 | B | T | Pattern timing, start/stop latency, UNKNOWN silent |
| FR-30 | Hub fault handling | TC-30 | B | T | Fault injection, missing key |
| FR-31 | Hub boot behavior | TC-31 | S | T | Reset, UNKNOWN, `HUB_BOOT`, offline from boot |
| FR-32 | Diagnostic counters | TC-32 | U/B | T | Crafted-packet run vs counters |

### 1.3 Provisioning

| Req | Title | Test | Level | Method | Test summary |
|---|---|---|---|---|---|
| FR-40 | Node provisioning | TC-40 | B | T | Persist across power cycles, out-of-range rejected |
| FR-41 | Hub provisioning | TC-41 | B | T | Persist, out-of-range rejected, only 5 registered nodes accepted |
| FR-42 | Key handling | TC-42 | R | I/T | Secret scan over all commits, build without key in source |
| FR-43 | `reset-counter` command | TC-43 | S | T | Replaced node dropped, then accepted after reset |
| FR-44 | Configurable timeouts | TC-44 | B | T | Changed values measured |
| FR-45 | Serial-only provisioning | TC-45 | S | T/I | Radio scan, no config service, no packet changes config |

### 1.4 Uplink

| Req | Title | Test | Level | Method | Test summary |
|---|---|---|---|---|---|
| FR-50 | Forwarding | TC-50 | S | T | 50 mixed events once each at the gateway |
| FR-51 | Message format | TC-51 | U/S | T | Schema validation, `hub_seq` gap-free |
| FR-52 | Buffered retry | TC-52 | S | T | 30 min outage with 60 and 120 events |
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
| NFR-05 | Motor safety | TC-74 | B | T | 1 h with injected faults, longest ON ≤ 400 ms, low at boot |
| NFR-06 | Counter durability | TC-75, TC-11 | B | T | 1000 random power cuts, strictly increasing log |
| NFR-07 | Uplink latency | TC-76 | S | T | 50 events within 5 s |
| NFR-08 | Stability | TC-77 | S | T | 24 h soak, resets and heap |
| NFR-09 | Toolchain | TC-78 | R | I/T | Clean build with pinned versions |
| NFR-10 | Packet size | TC-79 | R/B | I/T | 21 bytes, confirmed on air |
| NFR-11 | Battery (informational) | TC-80 | B | A | Current measured, life estimated, report only |
| NFR-12 | Traceability | TC-81 | R | I | Script checks every requirement ID has a test row |
| NFR-13 | Fail-safe default | TC-03, TC-16, TC-28 | U/B | T | Covered by the linked tests |

## 3. Coverage check

| Group | Requirements | With a test |
|---|---|---|
| Node FR-01 to FR-16 | 16 | 16 |
| Hub FR-20 to FR-32 | 13 | 13 |
| Provisioning FR-40 to FR-45 | 6 | 6 |
| Uplink FR-50 to FR-54 | 5 | 5 |
| Dashboard FR-60 to FR-63 | 4 | 4 |
| Non-functional NFR-01 to NFR-13 | 13 | 13 |
| **Total** | **57** | **57** |

## 4. Reverse index (tests shared by more than one requirement)

| Test | Requirements |
|---|---|
| TC-03 | FR-03, NFR-13 |
| TC-11 | FR-11, NFR-06 |
| TC-16 | FR-16, NFR-13 |
| TC-28 | FR-28, NFR-13 |
| TC-42 | FR-42, NFR-04 |
