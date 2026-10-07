# Smart PPE Compliance Firmware: Software Requirements Specification (SRS)

| Item | Value |
|---|---|
| Document | `docs/SRS_v1.1.md` |
| Version | 1.1 (APPROVED (S0)) |
| Scope | Firmware v1, procedural rebuild from scratch |
| Companions | `docs/protocol_spec_v1.md`, `docs/traceability_v1.1.md` |
| Branch | `feature/s0-docs-srs` (to be merged to `develop` after sign-off) |

---

## 1. Introduction

### 1.1 Purpose

This document defines what the Smart PPE Compliance firmware shall do, so that the node and hub code can be written and tested against it. Every requirement has an ID and a measurable acceptance criterion, and is linked to a verification test in `traceability_v1.1.md`. The packet-level details are in `protocol_spec_v1.md`, and the SRS refers to it instead of repeating it.

### 1.2 System overview

- **Vest hub:** one ESP32-S3 on the vest. Scans BLE continuously, verifies and tracks 5 nodes, decides compliance, drives a vibration motor, and forwards everything over Wi-Fi to a gateway/cloud and a simple web dashboard.
- **Nodes:** five ESP32-C3 devices: left boot, right boot, left glove, right glove, helmet. Left and right share one binary. Each runs in deep sleep and advertises wear/remove events and heartbeats.
- **Radio:** BLE advertisement-only, authenticated with a truncated HMAC-SHA256 and a per-node monotonic counter.

### 1.3 Key parameters (defaults; see protocol spec section 1.1)

| Parameter | Default |
|---|---|
| Heartbeat | 360 s |
| Grace period (trigger to transmission) | 25 s |
| Copies per transmission | 5, spaced 500 ms |
| Offline timeout | 2 × 360 s + 60 s margin = 780 s (margin tuned at test) |
| Motor pattern | ON 300 ms, OFF 700 ms (may be revised during development) |
| FSR thresholds | to WORN at ≥ 1537, to NOT_WORN at ≤ 88 |

### 1.4 Definitions

| Term | Meaning |
|---|---|
| WORN / NOT_WORN | Binary wear state of a node |
| UNKNOWN | Hub-internal state: no valid packet since hub boot. Never transmitted |
| OFFLINE | Hub state: no accepted packet for the offline timeout |
| Compliant | All 5 nodes are WORN |
| Confirmed state | Last wear state a node transmitted |
| Logical packet / copy | One heartbeat or event / one of its 5 identical transmissions |

### 1.5 Governing rules

1. Every requirement is testable, with an acceptance criterion (AC).
2. **Fail-safe default:** an unknown state counts as non-compliant. By stakeholder decision (D-04) UNKNOWN is non-compliant but does **not** start the motor alert, since it only occurs at power-on and reconnection.
3. The motor never runs continuously. It bips ON 300 ms / OFF 700 ms.
4. The real HMAC key is never committed.

### 1.6 Out of scope

Writing firmware, state machines and the test plan (next S0 chat), dashboard design. Hall sensors (v2). Over-the-air provisioning. Flash encryption and secure boot.

---

## 2. Functional requirements

### 2.1 Node (ESP32-C3)

| ID | Requirement | Acceptance criterion |
|---|---|---|
| FR-01 | The node SHALL load `node_id`, `vest_id`, key and timing parameters from NVS. Left and right nodes SHALL run the same binary, with identity set only at provisioning. | One binary flashed on two boards provisioned as `0x01` and `0x02` sends packets with the matching `node_id`. An unprovisioned board transmits 0 packets in 10 minutes. |
| FR-02 | Glove wear sensing: metal strip + transistor connected = WORN, open = NOT_WORN. | 20 connect/disconnect cycles on each glove node: candidate state matches contact in 100% of cycles (debug log). |
| FR-03 | Boot wear sensing with a force-sensitive resistor (FSR) converted to binary with hysteresis: NOT_WORN to WORN only at filtered count ≥ 1537, WORN to NOT_WORN only at ≤ 88, in between the state holds. With no history and a count in between, the state SHALL be NOT_WORN. | Injected count sequence 0, 88, 89, 800, 1536, 1537, 4095, 1536, 89, 88: state changes only at 1537 (to WORN) and 88 (to NOT_WORN). A power-on reading of 800 gives NOT_WORN. |
| FR-04 | Helmet two-factor sensing: WORN only if the crown push switch AND the chin-strap contact are both closed. Any other combination is NOT_WORN. | All 4 input combinations tested: only both-closed gives WORN. |
| FR-05 | A sensor change SHALL become the candidate state only after holding for `SETTLE_MS` (configurable 0 to 1000 ms, 0 allowed). | `SETTLE_MS=0`: candidate within one poll (≤ 50 ms). `SETTLE_MS=500`: a 400 ms pulse gives no candidate, a 600 ms change does. |
| FR-06 | Grace period: a candidate state that differs from the confirmed state SHALL be transmitted only after it has continuously differed for `GRACE_S` (25 s). If it returns to the confirmed state first, nothing is sent and the next change starts a new full timer. | (a) Change held 25 s: exactly one event at 25.0 ± 0.5 s. (b) Reverted at 10 s: 0 packets. (c) Change, revert at 10 s, change again at 12 s: event at 37 ± 0.5 s. (d) `GRACE_S=10`: event at 10 ± 0.5 s. |
| FR-07 | On grace expiry the node SHALL set the confirmed state and transmit an EVENT packet with the new state (both wear and remove). | 20 wear and 20 remove cycles: each produces one logical EVENT with the correct state and counter + 1. |
| FR-08 | After power-on the node SHALL decide WORN or NOT_WORN, wait `GRACE_S`, then transmit an EVENT with `BOOT=1`. It SHALL NOT transmit UNKNOWN or any packet before that. | Power-on worn and not worn: no packet before 25 s, then exactly one logical EVENT with `BOOT=1` and the correct state at 25.0 ± 0.5 s. |
| FR-09 | Heartbeat every `HEARTBEAT_S` (360 s), measured from the start of the previous transmission of any type, carrying the current confirmed state. | 10 consecutive heartbeats: every interval ≤ 378 s and mean 360 ± 18 s. After an event the next heartbeat is 360 ± 18 s later. |
| FR-10 | Every logical packet (event and heartbeat) SHALL be transmitted as 5 byte-identical copies, 500 ms (±50 ms) start-to-start, then no more. | Sniffer capture of 20 events and 20 heartbeats: exactly 5 identical copies each at 500 ± 50 ms, and no further copy in the following 10 s. |
| FR-11 | The counter SHALL be a uint32 incremented by 1 per logical packet, persisted in NVS with a 64-value reserved block, and advanced past the block on every boot. | Consecutive logical packets differ by 1 (copies equal). After 100 forced resets the counter never repeats or decreases. At most 1 NVS write per 64 packets (NVS write log). |
| FR-12 | The node SHALL authenticate each packet per protocol spec section 5 (HMAC-SHA256, mbedtls, first 8 bytes). | Node output matches Appendix A vectors with the test key. An independent Python verifier accepts 1000 of 1000 captured packets. |
| FR-13 | Every packet SHALL carry battery percent (0 to 100, `0xFF` if unavailable) and set `LOW_BATT` when ≤ 20%. | Bench supply at 4.2, 3.9, 3.6, 3.3 V: reported percent is non-increasing and `LOW_BATT` is set at ≤ 20%. With the ADC read failing, battery = `0xFF`. |
| FR-14 | The node SHALL be in deep sleep except during grace, transmission and boot. | Current profile shows deep sleep between activity. Awake time per event ≤ `GRACE_S` + 4 s. |
| FR-15 | After each confirmed event the node SHALL arm GPIO wake at the level opposite to the confirmed state (level-triggered wake), plus a timer wake for the next heartbeat. | 20 consecutive wear/remove cycles each wake the node from deep sleep with no missed change. |
| FR-16 | An invalid sensor reading SHALL be reported as NOT_WORN with `SENSOR_FAULT=1`, after the normal grace period. | With the sensor disconnected or the ADC read forced to fail: a NOT_WORN packet with `SENSOR_FAULT=1` is sent after 25 ± 0.5 s. |

### 2.2 Hub (ESP32-S3)

| ID | Requirement | Acceptance criterion |
|---|---|---|
| FR-20 | The hub SHALL scan continuously (no deliberate scan gaps). | The hub receives packets from all 5 nodes. A test beacon every 10 s for 1 h is received ≥ 99%. |
| FR-21 | Scan watchdog: if scanning makes no progress for 10 s the hub SHALL restart the scan. After 3 consecutive failed restarts it SHALL reboot. | Forced scan stall: scan resumes within 10 s and packets are received again. Three forced consecutive failures cause a reboot. |
| FR-22 | The hub SHALL validate each advertisement in the order of protocol spec section 2: format, version, `vest_id`, registered `node_id`, HMAC, counter. | Crafted packets with a bad length, bad AD header, wrong version, wrong vest, unknown node: 0 accepted. The matching drop counter increments for each. |
| FR-23 | The hub SHALL verify the 8-byte HMAC with a constant-time comparison and SHALL NOT touch the counter or state on failure. | The valid vector is accepted. All 136 single-bit-flipped variants of bytes 0 to 16: 0 accepted. `last_counter` unchanged after each. |
| FR-24 | Counter rule per node (protocol spec 6.2): `c > L` accept, `c == L` duplicate (drop silently), `c < L` drop and report `COUNTER_ANOMALY`. `last_counter` SHALL be persisted in NVS on every accepted packet, immediately after the packet is dispatched, from a non-blocking task. | 5 copies of one packet: 1 accepted, 4 dropped as duplicates, `last_seen` updated once. 100 old captured packets replayed after newer ones: 0 accepted and anomalies reported. After a hub reset the last accepted packet replayed is rejected. |
| FR-25 | The hub SHALL keep a registry of exactly 5 nodes (`node_id`, `last_counter`, `last_seen`, state, battery, flags), loaded from NVS at boot. | After a hub reset `last_counter` values are retained and all states are UNKNOWN. |
| FR-26 | Node state machine (protocol spec 8.2): UNKNOWN at boot. WORN or NOT_WORN from the `state` of any accepted packet (event or heartbeat). OFFLINE on timeout. Recovery on the next accepted packet. | Scripted packet sequences produce exactly the transitions in the spec table, including OFFLINE to WORN and UNKNOWN to NOT_WORN. |
| FR-27 | A node SHALL be declared OFFLINE when no packet has been accepted for 2 × `HEARTBEAT_S` + `OFFLINE_MARGIN_S` (default 780 s). | Node powered off: OFFLINE between 720 s and 785 s after its last packet. With margin 0: between 720 s and 725 s. Dropping exactly one heartbeat (all 5 copies) does NOT cause OFFLINE. |
| FR-28 | Compliance engine: COMPLIANT only when all 5 nodes are WORN. Every transition SHALL be recorded and reported. | All 5 WORN: COMPLIANT. For each node in turn, NOT_WORN, OFFLINE and UNKNOWN each give NON_COMPLIANT. Each transition is reported. |
| FR-29 | Motor alert: while any node is NOT_WORN or OFFLINE the motor SHALL bip ON 300 ms / OFF 700 ms. It starts immediately after the triggering packet or timeout, stops as soon as no node is NOT_WORN or OFFLINE, and UNKNOWN never starts it. | ON 300 ± 30 ms and OFF 700 ± 30 ms over 60 s (logic analyzer). Starts ≤ 200 ms after the accepted NOT_WORN packet. Stops ≤ 1 s after the last offending node recovers. No alert with all nodes UNKNOWN after hub boot. |
| FR-30 | Hub fault handling: scan stall (FR-21), task watchdog reset, NVS read failure, missing key. Faults SHALL be reported as `HUB_FAULT` when the uplink is available. A missing key or unreadable registry SHALL result in no packet being accepted, so nodes go OFFLINE and the alert runs. | Each fault injected: the hub recovers or reboots, the motor is never continuously on, and `HUB_FAULT` is reported. Missing key: all nodes OFFLINE within 785 s of boot and the motor alert is active. |
| FR-31 | On boot the hub SHALL set all nodes UNKNOWN, start the offline timer from boot, and report `HUB_BOOT`. | After reset: states UNKNOWN, no alert, `HUB_BOOT` received. A silent node is OFFLINE 780 ± 5 s after boot. |
| FR-32 | The hub SHALL keep the diagnostic counters in protocol spec 9.3 and include them in `HUB_STATUS`. | After a crafted-packet run the counters equal the number of injected packets of each kind. |

### 2.3 Provisioning

| ID | Requirement | Acceptance criterion |
|---|---|---|
| FR-40 | Node provisioning over serial: `node_id`, `vest_id`, key (32 bytes), `SETTLE_MS`, `GRACE_S`, `HEARTBEAT_S`, counter baseline. | Values persist across power cycles. An out-of-range value is rejected with an error and no change. |
| FR-41 | Hub provisioning over serial: `vest_id`, key, the 5 registered `node_id`s, Wi-Fi credentials, gateway endpoint (LAN IP and port), gateway token, `OFFLINE_MARGIN_S`. | Values persist across power cycles. Out-of-range values rejected. The hub accepts packets only from the 5 registered nodes. |
| FR-42 | Key handling: one key per vest, entered at provisioning from a file outside the repository. The real key SHALL NOT exist in source, build files or any commit. The repository SHALL contain only a placeholder example and gitignore rules. | A secret scan (`git grep`/gitleaks) over all commits on the branch finds 0 occurrences of the real key. Firmware builds with no key in source. |
| FR-43 | Hub serial command `reset-counter <node_id\|all>` sets `last_counter` to 0 and reports `COUNTER_RESET`. | A replaced node (counter 1) is dropped and flagged until the command is run, then accepted. `COUNTER_RESET` is reported. |
| FR-44 | Timeouts SHALL be configurable within the bounds in protocol spec 1.1. | `GRACE_S=10`, `HEARTBEAT_S=60`, `OFFLINE_MARGIN_S=0` each take effect and are measured at the configured value ± 5%. |
| FR-45 | Provisioning and configuration SHALL be serial-only. No radio configuration interface exists. | A BLE/Wi-Fi scan of a running node and hub shows no configuration service. No packet changes any configuration value. |

### 2.4 Uplink (hub to gateway)

| ID | Requirement | Acceptance criterion |
|---|---|---|
| FR-50 | The hub SHALL forward every accepted packet and every hub-generated message (`NODE_OFFLINE`, `NODE_REBOOT`, `COMPLIANCE`, `COUNTER_ANOMALY`, `COUNTER_RESET`, `HUB_BOOT`, `HUB_FAULT`, `HUB_STATUS`) to the gateway over Wi-Fi. | 50 mixed events: each appears exactly once at the gateway with correct fields. |
| FR-51 | Messages SHALL use the format in protocol spec section 10, including `hub_seq` and `age_ms`. | Every received message validates against the schema. `hub_seq` is gap-free in a loss-free run. |
| FR-52 | Buffered retry: while the uplink is down the hub SHALL buffer up to 100 messages in order, retry with backoff 1 s up to 30 s, drop the oldest on overflow and report the dropped count. | Wi-Fi off for 30 min with 60 events: all 60 delivered in order after restore. With 120 events: the oldest 20 dropped and `dropped=20` reported. |
| FR-53 | Wi-Fi activity SHALL NOT delay the BLE scan or the motor alert. | With Wi-Fi association failing continuously, FR-29 timings and FR-20 reception are unchanged. |
| FR-54 | The hub SHALL send `HUB_STATUS` every 60 s with uptime, per-node state, diagnostics and buffer fill. | 10 consecutive `HUB_STATUS` intervals are 60 ± 3 s. |

### 2.5 Dashboard (functional content only; design out of scope)

| ID | Requirement | Acceptance criterion |
|---|---|---|
| FR-60 | The dashboard SHALL show live status per node (state, battery percent, low-battery indicator, last seen, online/offline) and the vest compliance. | Each field matches the injected data for all 5 nodes, and updates ≤ 5 s after a message arrives. |
| FR-61 | The gateway computer SHALL timestamp each message on arrival, and the dashboard SHALL show the event time as arrival time minus `age_ms`. | A buffered event delayed by a known 120 s shows an event time within ±1 s of the real event. |
| FR-62 | The dashboard SHALL keep an event history (timestamp, node, type, state, including compliance-restored events) of at least 1000 events, newest first. | 1000 injected events are all listed in order with timestamps. A compliance-restored event appears with its timestamp. |
| FR-63 | The dashboard SHALL show an alert for NOT_WORN, OFFLINE, COUNTER_ANOMALY, and for no `HUB_STATUS` within 180 s, and clear it when resolved. | Each condition injected raises its alert within 5 s (hub silence: within 185 s) and clears when resolved. |

---

## 3. Non-functional requirements

| ID | Requirement | Acceptance criterion |
|---|---|---|
| NFR-01 | **Alert latency:** from a physical remove to the start of the motor alert ≤ `GRACE_S` + `SETTLE_MS` + 3 s (28 s at defaults). | 30 remove trials per node type: 100% start within 28 s. |
| NFR-02 | **Reception reliability:** with the 5 copies, at least one copy is accepted for each event. | ≥ 99% of 100 events per node, at ≤ 2 m line of sight (AS-07). |
| NFR-03 | **Hub processing:** each received advertisement is filtered, verified and dispatched in ≤ 10 ms. A burst of 25 copies (5 nodes × 5) is handled with no loss. | Timestamp instrumentation: max ≤ 10 ms per packet over 1000 packets. Burst test: 25 of 25 processed. |
| NFR-04 | **Security:** forged, modified, replayed, wrong-vest and unregistered packets are never accepted. The real key is never in the repository. | 1000 random forged packets and 100 replays: 0 accepted. Secret scan clean (FR-42). |
| NFR-05 | **Motor safety:** the motor output is never on continuously. It is off at boot and in every fault, and a guard forces it off after 400 ms. | Over 1 h with injected resets, scan stall and WDT reset: the maximum continuous ON time is ≤ 400 ms and the output is low at boot. |
| NFR-06 | **Counter durability:** the node counter never repeats or decreases across power loss. | 1000 random power cuts over a run: the counter log is strictly increasing. |
| NFR-07 | **Uplink latency:** with Wi-Fi up, a message reaches the dashboard ≤ 5 s after the hub accepts the packet. | 50 events: 100% within 5 s. |
| NFR-08 | **Stability:** no unplanned reset on hub or nodes in a soak run. | 24 h run: 0 unplanned resets, minimum free heap does not fall > 1% of its start value. |
| NFR-09 | **Toolchain:** builds with Arduino IDE 2.3.10, esp32 core 3.0.7, NimBLE-Arduino 2.5.1, with no errors. | Clean build of node and hub with exactly these versions. |
| NFR-10 | **Packet size:** advertising data ≤ 31 bytes. | Inspection: 21 bytes, confirmed on air by sniffer. |
| NFR-11 | **Battery (informational, TBD, not a v1.1 acceptance gate):** node 1500 mAh, theoretical about 41 days, real world 30 to 40 days. Hub 2200 mAh, theoretical about 1.1 days, real world 20 to 24 hours. Open to major change. | Current is measured and the estimated life is reported. Pass/fail limits are set in a later revision. |
| NFR-12 | **Traceability:** every requirement has a linked verification test. | 100% of requirements in this document appear in `traceability_v1.1.md` with a test ID. |
| NFR-13 | **Fail-safe default:** an unknown or invalid state counts as non-compliant. | Covered by FR-03, FR-16 and FR-28. See the traceability table. |

---

## 4. Constraints

- Hardware: ESP32-S3 hub, ESP32-C3 nodes ×5. Deep-sleep GPIO wake on the C3 is level-triggered (GPIO0 to 5).
- Toolchain (accepted): Arduino IDE 2.3.10, esp32 core 3.0.7, NimBLE-Arduino 2.5.1. The S0 BLE spike passed T1 to T8.
- Security primitives: mbedtls HMAC-SHA256, 8-byte truncated tag.
- Wear sensing v1 as in FR-02 to FR-04. Hall sensors are v2.

---

## 5. Decisions recorded for sign-off

| ID | Decision |
|---|---|
| D-01 | FSR hysteresis: to WORN at ≥ 1537, to NOT_WORN at ≤ 88, hold in between. |
| D-02 | Helmet: either factor open = NOT_WORN. |
| D-03 | A state change that reverts in the grace period is cancelled silently, and the timer restarts on a new change. |
| D-04 | UNKNOWN occurs only at power-on and reconnection. It is never transmitted and does not start the alert, but it is non-compliant. (may be revised during development) |
| D-05 | The 5 copies share one counter. The hub accepts the first and ignores the rest. Same for heartbeats. |
| D-06 | Offline margin to be tuned at test. 60 s is the proposed default. |
| D-07 | Battery targets are TBD for now. |
| D-08 | The gateway computer stamps each message on arrival. The hub keeps no clock for event timestamps. Buffered messages carry `age_ms`. |
| D-09 | Compliance needs all 5 nodes present and worn. The required set is fixed at 5 in v1. |
| D-10 | A NOT_WORN or OFFLINE node starts the vest motor alert. UNKNOWN does not. |
| D-11 | The counter never decreases. A lower counter is dropped and flagged. Resync is by operator re-provision, with no over-the-air window. |
| D-12 | One HMAC key per vest. |
| D-13 | The hub persists last_counter immediately after dispatch, from a non-blocking task. |
| D-14 | The v1 uplink is HTTP with a shared token on the local network. HTTPS with SNTP is a later option. |

---

## 6. Assumptions

| ID | Assumption |
|---|---|
| AS-01 | A vest has a fixed set of exactly 5 nodes. A configurable required set is deferred. |
| AS-02 | `SETTLE_MS` defaults to 0 and is bounded 0 to 1000 ms. |
| AS-03 | The node stays awake during grace, polling its sensor about every 50 ms. It deep-sleeps otherwise. |
| AS-04 | How boots wake on a change (external comparator or periodic ADC sampling) is a design item. The behavioral requirement is NFR-01 latency. |
| AS-05 | Advertising burst 100 ms and interval 20 ms are provisional, to be confirmed against the S0 spike results. |
| AS-06 | The hub persists `last_counter` on every accepted packet. NVS wear levelling makes this acceptable at about 1,200 writes per day. |
| AS-07 | The reliability test (NFR-02) is at ≤ 2 m line of sight with no deliberate interference. |
| AS-08 | The scan watchdog is 10 s, and the stall restart count is 3. |
| AS-09 | The v1 uplink is HTTP POST with JSON, with a shared token header, from the hub to a gateway computer on the local network. HTTPS with SNTP time sync on the hub is a later option. To be rechecked in S3 and S7 with the Wi-Fi plus BLE coexistence check. |
| AS-10 | Company ID `0xFFFF` is acceptable for v1. |
| AS-11 | Battery percent is derived from measured cell voltage with a fixed lookup. |
| AS-12 | Dashboard history is at least 1000 events. The uplink buffer is 100 messages. `HUB_STATUS` is every 60 s. |
| AS-13 | The key, Wi-Fi credentials and gateway token are stored in NVS in plaintext. Flash encryption and secure boot are out of scope for v1. |
| AS-14 | "5 retransmissions" is implemented as 5 copies in total (1 original + 4 repeats), confirmed by the project owner. |
| AS-15 | The node's deep-sleep timer accuracy is within ±5%. |

---

## 7. Risks

| ID | Risk | Note |
|---|---|---|
| R-01 | A dead or failed node is only detected as OFFLINE after up to about 780 s. | Accepted by design. The margin may be tuned at test. |
| R-02 | After a hub reset, nodes are UNKNOWN (no alert) until their next packet, up to 360 s for a non-compliant node. | Direct consequence of D-04. Hub resets are reported (`HUB_BOOT`) and should be rare (NFR-08). |
| R-03 | The key is stored in plaintext NVS (AS-13). | Physical access is out of the v1 threat model. |
| R-04 | Wi-Fi and BLE share the S3 radio, which can reduce scan reception. | Mitigated by FR-53. Rechecked in S3 and S7. |
| R-05 | Hub battery life is only 20 to 24 hours in the real-world estimate. | Informational (NFR-11). May drive hardware change. |

---

## 8. Change from the draft feature list

- Added: packet `vest_id`, version and type in `flags`, `BOOT`, `LOW_BATT` and `SENSOR_FAULT` flag bits; `HUB_STATUS`; hub diagnostic counters; `reset-counter` command; `age_ms` on uplink messages.
- Removed: the configurable required PPE set (fixed at 5 in v1).
- Corrected: duplicated `battery` and `counter` rows in the draft packet table.
