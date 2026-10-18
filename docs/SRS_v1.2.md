# Smart PPE Compliance Firmware: Software Requirements Specification (SRS)

| Item | Value |
|---|---|
| Document | `docs/SRS_v1.2.md` |
| Version | 1.2 (release candidate for sign-off; resolves S0 findings F-01 to F-20) |
| Scope | Firmware v1, procedural rebuild from scratch |
| Companions | `docs/protocol_spec_v1.1.md`, `docs/traceability_v1.2.md` |
| Branch | `feature/s0-statemachines-testplan` (merged to `develop` after sign-off) |

### Revision history

| Version | Change |
|---|---|
| 1.1 | S0 draft, approved and pushed (`s0-docs-done`). Review edits: counter persisted after dispatch (FR-24), gateway timestamps (FR-61), HTTP uplink on the local network (AS-09), provisioning list (FR-41), motor pattern note, D-08, D-13, D-14, AS-13, AS-14. |
| 1.2 | Resolves S0 findings F-01 to F-20 (`docs/s0_findings_v1.md`). New: FR-17, FR-46, D-15 to D-19, AS-16 to AS-18, R-06. Changed: FR-06, FR-08, FR-09, FR-11, FR-15, FR-16, FR-21, FR-22, FR-24, FR-25, FR-27, FR-28, FR-30, FR-32, FR-41, FR-50, FR-51, FR-52, FR-54, FR-61, NFR-05, NFR-13, D-04, AS-09, AS-13 to AS-15. The wire format is unchanged. |

---

## 1. Introduction

### 1.1 Purpose

This document defines what the Smart PPE Compliance firmware shall do, so that the node and hub code can be written and tested against it. Every requirement has an ID and a measurable acceptance criterion, and is linked to a verification test in `traceability_v1.2.md`. The packet-level details are in `protocol_spec_v1.1.md`, and the SRS refers to it instead of repeating it.

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
| Key-fault motor pattern | 3 × (ON 100 ms, OFF 100 ms), then OFF 1700 ms |
| Uplink retry | 1 s, doubling to 30 s; HTTP timeout 2 s |
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
| Boot | Any node start whose reset reason is not deep-sleep wake, or a deep-sleep wake whose RTC context fails its magic and CRC check. A deep-sleep wake restores the node state from RTC memory and is not a boot |
| Gateway | A computer on the local network that receives hub messages, stamps their arrival time and forwards them to the cloud |
| NODE_FAULT | Node state after a fault it cannot recover from (FR-17). The node is silent and the hub sees it OFFLINE |

### 1.5 Governing rules

1. Every requirement is testable, with an acceptance criterion (AC).
2. **Fail-safe default:** an unknown state counts as non-compliant. By stakeholder decision (D-04) UNKNOWN is non-compliant but does **not** start the motor alert, since it only occurs after hub power-on or reset.
3. The motor never runs continuously. It bips ON 300 ms / OFF 700 ms (alert) or runs the `KEY_FAULT` pattern (FR-30).
4. The real HMAC key is never committed.

### 1.6 Out of scope

Writing firmware, dashboard visual design. Hall sensors (v2). Over-the-air provisioning. Flash encryption and secure boot. A hub event history ring (dropped, D-19).

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
| FR-06 | Grace period: a candidate state that differs from the confirmed state SHALL be transmitted only after it has continuously differed for `GRACE_S` (25 s). If it returns to the confirmed state first, nothing is sent and the next change starts a new full timer. The grace timer runs from the candidate change. If it expires while a transmission sequence is in progress, the EVENT starts when that sequence ends. | (a) Change held 25 s: exactly one event at 25.0 ± 0.5 s. (b) Reverted at 10 s: 0 packets. (c) Change, revert at 10 s, change again at 12 s: event at 37 ± 0.5 s. (d) `GRACE_S=10`: event at 10 ± 0.5 s. (e) `GRACE_S=1` and a change 0.5 s into a running sequence: the new EVENT starts after that sequence ends and the copies never interleave. |
| FR-07 | On grace expiry the node SHALL set the confirmed state and transmit an EVENT packet with the new state (both wear and remove). | 20 wear and 20 remove cycles: each produces one logical EVENT with the correct state and counter + 1. |
| FR-08 | After a boot (section 1.4) the node SHALL decide WORN or NOT_WORN, wait `GRACE_S`, then transmit an EVENT with `BOOT=1`. It SHALL NOT transmit UNKNOWN or any packet before that. If the candidate changes during the wait, the timer restarts, and `SETTLE_MS` is not applied to the first reading. A deep-sleep wake is not a boot. | (a) Power-on worn and not worn: no packet before 25 s, then exactly one logical EVENT with `BOOT=1` and the correct state at 25.0 ± 0.5 s. (b) Candidate changed 10 s after power-on: the `BOOT=1` EVENT is sent 25 ± 0.5 s after the change, with the new state. |
| FR-09 | Heartbeat every `HEARTBEAT_S` (360 s), measured from the start of the previous transmission of any type, carrying the current confirmed state. A heartbeat due while the node is in grace SHALL be sent at its due time with the confirmed state, and grace continues. | 10 consecutive heartbeats: every interval ≤ 378 s and mean 360 ± 18 s. After an event the next heartbeat is 360 ± 18 s later. With `HEARTBEAT_S=60` and a change 50 s after the last transmission: a heartbeat with the old confirmed state at 60 ± 3 s, then the EVENT at 75 ± 0.5 s. |
| FR-10 | Every logical packet (event and heartbeat) SHALL be transmitted as 5 byte-identical copies, 500 ms (±50 ms) start-to-start, then no more. | Sniffer capture of 20 events and 20 heartbeats: exactly 5 identical copies each at 500 ± 50 ms, and no further copy in the following 10 s. |
| FR-11 | The counter SHALL be a uint32 incremented by 1 per logical packet, persisted in NVS with a 64-value reserved block, and advanced past the block on every boot. A deep-sleep wake is not a boot: the counter, ceiling, confirmed state, heartbeat deadline and `sensor_fault` are restored from RTC memory, with no NVS write and no `BOOT=1`. | Consecutive logical packets differ by 1 (copies equal). After 100 forced resets the counter never repeats or decreases. At most 1 NVS write per 64 packets (NVS write log) across at least 20 heartbeat wakes, and `BOOT=1` appears only on the first logical packet after a reset. |
| FR-12 | The node SHALL authenticate each packet per protocol spec section 5 (HMAC-SHA256, mbedtls, first 8 bytes). | Node output matches Appendix A vectors with the test key. An independent Python verifier accepts 1000 of 1000 captured packets. |
| FR-13 | Every packet SHALL carry battery percent (0 to 100, `0xFF` if unavailable) and set `LOW_BATT` when ≤ 20%. | Bench supply at 4.2, 3.9, 3.6, 3.3 V: reported percent is non-increasing and `LOW_BATT` is set at ≤ 20%. With the ADC read failing, battery = `0xFF`. |
| FR-14 | The node SHALL be in deep sleep except during grace, transmission and boot. | Current profile shows deep sleep between activity. Awake time per event ≤ `GRACE_S` + 4 s. |
| FR-15 | After each confirmed event the node SHALL arm GPIO wake at the level opposite to the confirmed state (level-triggered wake), plus a timer wake for the next heartbeat. After 2 consecutive GPIO wakes with no candidate change, the node SHALL arm a timer-poll wake (period ≤ 1 s, set in S2) until the reading leaves the condition, then re-arm the GPIO wake. | 20 consecutive wear/remove cycles each wake the node from deep sleep with no missed change. An FSR count held in the dead band, and a helmet with only one contact closed, for 10 s each: at most 2 consecutive GPIO wakes before the node switches to timer-poll, and a real change is still detected within the NFR-01 budget. |
| FR-16 | The node SHALL monitor the pair (`state`, `sensor_fault`). A change in either, including the fault clearing, starts grace and produces an EVENT after `GRACE_S`. For FSR nodes a sensor fault is an ADC read error or timeout, or a count outside the plausible range set in S2. Glove and helmet contacts are digital and have no fault detection in v1 (D-15). A faulty sensor is reported as NOT_WORN with `SENSOR_FAULT=1`. | FSR node with the ADC read forced to fail, even when already NOT_WORN: a packet with `SENSOR_FAULT=1` is sent 25 ± 0.5 s later. Clearing the fault sends an EVENT with `SENSOR_FAULT=0` 25 ± 0.5 s later. Gloves and helmet: not applicable (a disconnected contact reads NOT_WORN, see R-06). |
| FR-17 | Node faults: a counter at its limit (protocol spec 6.1.5), an NVS counter-block write failure, or a BLE start failure after one retry SHALL put the node in `NODE_FAULT`. The node stops transmitting and logs the cause on serial. It SHALL NOT transmit without a persisted counter block, so a counter can never repeat. The hub sees the node as OFFLINE and alerts. | With each fault forced in turn: 0 packets are sent after the fault, the cause is on serial, the node is OFFLINE at the hub within 785 s and the motor alert is active. The counter log never repeats a value. |

### 2.2 Hub (ESP32-S3)

| ID | Requirement | Acceptance criterion |
|---|---|---|
| FR-20 | The hub SHALL scan continuously (no deliberate scan gaps). | The hub receives packets from all 5 nodes. A test beacon every 10 s for 1 h is received ≥ 99%. |
| FR-21 | Scan watchdog: the hub SHALL poll the scanner state every 1 s. If the scanner is not active, or the host stack reports a sync loss, for 10 s the hub SHALL restart the scan. After 3 consecutive failed restarts it SHALL reboot. Receiving no advertisements is not a stall. | Forced scan stall (test hook): the scan resumes within 10 s and packets are received again. Three forced consecutive failures cause a reboot. With no BLE traffic at all for 10 min and the scanner active: no restart and no reboot. |
| FR-22 | The hub SHALL validate each advertisement in the order of protocol spec section 2: format, version, `vest_id`, registered `node_id`, HMAC, `state`, counter. | Crafted packets with a bad length, bad AD header, wrong version, wrong vest, unknown node: 0 accepted. A packet with a valid HMAC and `state` 0x02: 0 accepted. The matching drop counter increments for each. |
| FR-23 | The hub SHALL verify the 8-byte HMAC with a constant-time comparison and SHALL NOT touch the counter or state on failure. | The valid vector is accepted. All 136 single-bit-flipped variants of bytes 0 to 16: 0 accepted. `last_counter` unchanged after each. |
| FR-24 | Counter rule per node (protocol spec 6.2): `c > L` accept, `c == L` duplicate (drop silently), `c < L` drop and report `COUNTER_ANOMALY`. `last_counter` SHALL be persisted in NVS on every accepted packet, immediately after the packet is dispatched, from a non-blocking task. | 5 copies of one packet: 1 accepted, 4 dropped as duplicates, `last_seen` updated once. 100 old captured packets replayed after newer ones: 0 accepted and anomalies reported. After a hub reset (at least 2 s after the last copy) the last accepted packet replayed is rejected. |
| FR-25 | The hub SHALL keep a registry of exactly 5 nodes (`node_id`, `last_counter`, `last_seen`, state, battery, flags), loaded from NVS at boot. The slots for `node_id` 1 to 5 always exist (compile-time list). | After a hub reset `last_counter` values are retained and all states are UNKNOWN. |
| FR-26 | Node state machine (protocol spec 8.2): UNKNOWN at boot. WORN or NOT_WORN from the `state` of any accepted packet (event or heartbeat). OFFLINE on timeout. Recovery on the next accepted packet. | Scripted packet sequences produce exactly the transitions in the spec table, including OFFLINE to WORN and UNKNOWN to NOT_WORN. |
| FR-27 | A node SHALL be declared OFFLINE when no packet has been accepted for 2 × `HEARTBEAT_S` + `OFFLINE_MARGIN_S` (default 780 s). | Node powered off: OFFLINE between 780 s and 785 s after its last packet at the default margin. With margin 0: between 720 s and 725 s. Dropping exactly one heartbeat (all 5 copies) does NOT cause OFFLINE. |
| FR-28 | Compliance engine: COMPLIANT only when all 5 nodes are WORN. A `COMPLIANCE` message SHALL be sent on every change of the vest state (COMPLIANT to NON_COMPLIANT and back). The initial NON_COMPLIANT at hub boot is not a transition. | All 5 WORN: COMPLIANT. For each node in turn, NOT_WORN, OFFLINE and UNKNOWN each give NON_COMPLIANT. Each vest-level change sends exactly one `COMPLIANCE` message with the new state, and none is sent at hub boot. |
| FR-29 | Motor alert: while any node is NOT_WORN or OFFLINE the motor SHALL bip ON 300 ms / OFF 700 ms. It starts immediately after the triggering packet or timeout, stops as soon as no node is NOT_WORN or OFFLINE, and UNKNOWN never starts it. | ON 300 ± 30 ms and OFF 700 ± 30 ms over 60 s (logic analyzer). Starts ≤ 200 ms after the accepted NOT_WORN packet. Stops ≤ 1 s after the last offending node recovers. No alert with all nodes UNKNOWN after hub boot. |
| FR-30 | Hub fault handling: scan stall (FR-21), task watchdog reset, NVS read failure, NVS write failure, missing key. Faults SHALL be logged on serial and reported as `HUB_FAULT` with a fault code when the uplink is available. A missing key SHALL raise `HUB_FAULT` (`NO_KEY`) immediately at boot, and the motor SHALL run the `KEY_FAULT` pattern (3 × ON 100 ms / OFF 100 ms, then OFF 1700 ms, repeating) until a key is provisioned and the hub restarts. No packet is accepted without a key, and the `KEY_FAULT` pattern has priority over the normal alert pattern. An unreadable registry SHALL leave the node slots 1 to 5 present with no accepted state, so nodes go OFFLINE and the alert runs. A hub NVS write failure SHALL be counted and reported (`NVS_WRITE`) and the hub continues on RAM counters. After a reboot caused by a fault, the fault code is kept in RTC memory and reported after boot. | Each fault injected: the hub recovers or reboots, the motor is never continuously on (ON ≤ 400 ms), and `HUB_FAULT` with the right code is reported. Missing key: `HUB_FAULT` (`NO_KEY`) within 2 s of boot (serial log when no uplink) and the `KEY_FAULT` pattern with ON 100 ± 20 ms, OFF 100 ± 20 ms (×3) then OFF 1700 ± 100 ms. Unreadable registry: all nodes OFFLINE within 785 s of boot and the motor alert is active. |
| FR-31 | On boot the hub SHALL set all nodes UNKNOWN, start the offline timer from boot, and report `HUB_BOOT`. | After reset: states UNKNOWN, no alert, `HUB_BOOT` received. A silent node is OFFLINE 780 ± 5 s after boot. |
| FR-32 | The hub SHALL keep the diagnostic counters in protocol spec 9.3 (global and per node) and include them in `HUB_STATUS`. | After a crafted-packet run the counters equal the number of injected packets of each kind. A packet with a valid HMAC and an invalid `state` increments `drop_state`. |

### 2.3 Provisioning

| ID | Requirement | Acceptance criterion |
|---|---|---|
| FR-40 | Node provisioning over serial: `node_id`, `vest_id`, key (32 bytes), `SETTLE_MS`, `GRACE_S`, `HEARTBEAT_S`, counter baseline. | Values persist across power cycles. An out-of-range value is rejected with an error and no change. |
| FR-41 | Hub provisioning over serial: `vest_id`, key, Wi-Fi credentials, gateway endpoint (LAN IP and port), gateway token, `HEARTBEAT_S`, `OFFLINE_MARGIN_S` (recommended ≥ 40 s). The 5 node slots (`node_id` 1 to 5) are fixed. | Values persist across power cycles. Out-of-range values rejected. `OFFLINE_TIMEOUT_S` follows the provisioned `HEARTBEAT_S` (a hub set to 60 s declares OFFLINE at 120 s plus margin). The hub accepts packets only from node ids 1 to 5. |
| FR-42 | Key handling: one key per vest, entered at provisioning from a file outside the repository. The real key SHALL NOT exist in source, build files or any commit. The repository SHALL contain only a placeholder example and gitignore rules. | A secret scan (`git grep`/gitleaks) over all commits on the branch finds 0 occurrences of the real key. Firmware builds with no key in source. |
| FR-43 | Hub serial command `reset-counter <node_id\|all>` sets `last_counter` to 0 and reports `COUNTER_RESET`. | A replaced node (counter 1) is dropped and flagged until the command is run, then accepted. `COUNTER_RESET` is reported. |
| FR-44 | Timeouts SHALL be configurable within the bounds in protocol spec 1.1. | `GRACE_S=10`, `HEARTBEAT_S=60`, `OFFLINE_MARGIN_S=0` each take effect and are measured at the configured value ± 5%. |
| FR-45 | Provisioning and configuration SHALL be serial-only. No radio configuration interface exists. | A BLE/Wi-Fi scan of a running node and hub shows no configuration service. No packet changes any configuration value. |
| FR-46 | Factory reset: the serial command `factory-reset CONFIRM` on a node or the hub SHALL erase its provisioning data and counters and restart it unprovisioned. An unprovisioned node transmits nothing. An unprovisioned hub raises `HUB_FAULT` (`NO_KEY`). After a node factory reset the operator runs `reset-counter` on the hub (FR-43). | After the command and a reboot: a node sends 0 packets in 10 min and a hub runs the `KEY_FAULT` pattern. Re-provisioning restores normal operation. The command without `CONFIRM` changes nothing. |

### 2.4 Uplink (hub to gateway)

| ID | Requirement | Acceptance criterion |
|---|---|---|
| FR-50 | The hub SHALL forward every accepted packet and every hub-generated message (`NODE_OFFLINE`, `NODE_REBOOT`, `COMPLIANCE`, `COUNTER_ANOMALY`, `COUNTER_RESET`, `HUB_BOOT`, `HUB_FAULT`, `HUB_STATUS`) to the gateway over Wi-Fi. `hub_seq` starts at 0 at each hub boot and `HUB_BOOT` is the first message. The gateway de-duplicates retried messages on (boot epoch, `hub_seq`). | 50 mixed events: each appears exactly once at the gateway with correct fields. A POST whose response is lost and is retried is stored once. |
| FR-51 | Messages SHALL use the format in protocol spec section 10, including `hub_seq`, `age_ms` and the per-type `detail` object. The schema is published as `tools/schema/uplink.schema.json` (S1). | Every received message validates against the schema. `hub_seq` is gap-free within a boot in a loss-free run. |
| FR-52 | Buffered retry: while the uplink is down the hub SHALL buffer up to 100 messages in order, retry with backoff 1 s doubling up to 30 s, and drop the oldest on overflow. At most one `HUB_STATUS` is held in the buffer: a newer one replaces the older (counted as superseded, not dropped). The hub SHALL report the `dropped` and `superseded` counts in `HUB_STATUS`. | After the uplink is restored, delivered messages are in `hub_seq` order, the missing messages are exactly the oldest ones, and the number of missing `hub_seq` values equals `dropped` + `superseded`. Controlled setup (nodes at `HEARTBEAT_S=3600`, Wi-Fi off 30 min): 60 events are all delivered. With 120 events, `dropped` equals the messages generated during the outage (excluding superseded `HUB_STATUS`) minus 100. |
| FR-53 | Wi-Fi activity SHALL NOT delay the BLE scan or the motor alert. | With Wi-Fi association failing continuously, FR-29 timings and FR-20 reception are unchanged. |
| FR-54 | The hub SHALL send `HUB_STATUS` every 60 s with uptime, per-node state, diagnostics and buffer fill (fields in protocol spec 10.1). | 10 consecutive `HUB_STATUS` intervals are 60 ± 3 s. |

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
| NFR-05 | **Motor safety:** the motor output is never on continuously. It is off at boot and in every fault, and a guard forces it off after 400 ms. A guard trip is logged, reported as `HUB_FAULT` (`GUARD`), and the pattern resumes at the next ON edge. | Over 1 h with injected resets, scan stall and WDT reset: the maximum continuous ON time is ≤ 400 ms and the output is low at boot. A forced guard trip is reported. |
| NFR-06 | **Counter durability:** the node counter never repeats or decreases across power loss. | 1000 random power cuts over a run: the counter log is strictly increasing. |
| NFR-07 | **Uplink latency:** with Wi-Fi up, a message reaches the dashboard ≤ 5 s after the hub accepts the packet. | 50 events: 100% within 5 s. |
| NFR-08 | **Stability:** no unplanned reset on hub or nodes in a soak run. | 24 h run: 0 unplanned resets, minimum free heap does not fall > 1% of its start value. |
| NFR-09 | **Toolchain:** builds with Arduino IDE 2.3.10, esp32 core 3.0.7, NimBLE-Arduino 2.5.1, with no errors. | Clean build of node and hub with exactly these versions. |
| NFR-10 | **Packet size:** advertising data ≤ 31 bytes. | Inspection: 21 bytes, confirmed on air by sniffer. |
| NFR-11 | **Battery (informational, TBD, not a v1.2 acceptance gate):** node 1500 mAh, theoretical about 41 days, real world 30 to 40 days. Hub 2200 mAh, theoretical about 1.1 days, real world 20 to 24 hours. Open to major change. | Current is measured and the estimated life is reported. Pass/fail limits are set in a later revision. |
| NFR-12 | **Traceability:** every requirement has a linked verification test. | 100% of requirements in this document appear in `traceability_v1.2.md` with a test ID. |
| NFR-13 | **Fail-safe default:** an unknown or invalid state counts as non-compliant. | Covered by FR-03, FR-16, FR-28 and FR-30. See the traceability table. |

---

## 4. Constraints

- Hardware: ESP32-S3 hub, ESP32-C3 nodes ×5. Deep-sleep GPIO wake on the C3 is level-triggered (GPIO0 to 5).
- Toolchain (accepted): Arduino IDE 2.3.10, esp32 core 3.0.7, NimBLE-Arduino 2.5.1. The S0 BLE spike passed T1 to T8.
- Security primitives: mbedtls HMAC-SHA256, 8-byte truncated tag.
- Wear sensing v1 as in FR-02 to FR-04. Hall sensors are v2.
- Uplink v1: HTTP POST with JSON and a shared token to a gateway computer on the local network (D-14, AS-09).

---

## 5. Decisions recorded for sign-off

| ID | Decision |
|---|---|
| D-01 | FSR hysteresis: to WORN at ≥ 1537, to NOT_WORN at ≤ 88, hold in between. |
| D-02 | Helmet: either factor open = NOT_WORN. |
| D-03 | A state change that reverts in the grace period is cancelled silently, and the timer restarts on a new change. |
| D-04 | UNKNOWN occurs only after hub power-on or reset. It is never transmitted and does not start the alert, but it is non-compliant. May be revised during development. |
| D-05 | The 5 copies share one counter. The hub accepts the first and ignores the rest. Same for heartbeats. |
| D-06 | Offline margin to be tuned at test. 60 s is the proposed default. |
| D-07 | Battery targets are TBD for now. |
| D-08 | The gateway computer stamps each message on arrival. The hub keeps no clock for event timestamps. Buffered messages carry `age_ms`. |
| D-09 | Compliance needs all 5 nodes present and worn. The required set is fixed at 5 in v1. |
| D-10 | A NOT_WORN or OFFLINE node starts the vest motor alert. UNKNOWN does not. |
| D-11 | The counter never decreases. A lower counter is dropped and flagged. Resync is by operator re-provision, with no over-the-air window. |
| D-12 | One HMAC key per vest. |
| D-13 | The hub persists `last_counter` immediately after dispatch, from a non-blocking task. |
| D-14 | The v1 uplink is HTTP with a shared token on the local network. HTTPS with SNTP time sync is a later option. |
| D-15 | Glove and helmet contacts have no sensor-fault detection in v1. A disconnected contact reads NOT_WORN and raises the alert. |
| D-16 | A deep-sleep wake is not a boot. Node state is kept in RTC memory, trusted only after a deep-sleep wake with a valid magic and CRC. |
| D-17 | `hub_seq` restarts at 0 at each hub boot. The gateway de-duplicates on (boot epoch, `hub_seq`). |
| D-18 | A missing hub key raises `HUB_FAULT` (`NO_KEY`) at once and runs the `KEY_FAULT` motor pattern. May be revised during development. |
| D-19 | The hub event history ring is dropped from v1 (history is kept by the gateway and dashboard). A `factory-reset` serial command is added (FR-46). |

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
| AS-09 | The v1 uplink is HTTP POST with JSON and a shared token header, from the hub to a gateway computer on the local network. HTTPS with SNTP time sync on the hub is a later option. To be rechecked in S3 and S7 with the Wi-Fi plus BLE coexistence check. |
| AS-10 | Company ID `0xFFFF` is acceptable for v1. |
| AS-11 | Battery percent is derived from measured cell voltage with a fixed lookup. |
| AS-12 | Dashboard history is at least 1000 events. The uplink buffer is 100 messages. `HUB_STATUS` is every 60 s. |
| AS-13 | The key, Wi-Fi credentials and gateway token are stored in NVS in plaintext. Flash encryption and secure boot are out of scope for v1. |
| AS-14 | "5 retransmissions" is implemented as 5 copies in total (1 original + 4 repeats), confirmed by the project owner. |
| AS-15 | The node's deep-sleep timer accuracy is within ±5%. The RTC slow-clock accuracy is measured in S4 (TC-09) and calibrated if it exceeds ±5%. |
| AS-16 | Uplink timing: retry backoff 1 s doubling to 30 s (reset on success), HTTP timeout 2 s, Wi-Fi reconnect 5 s doubling to 30 s. |
| AS-17 | Deep-sleep wake to application start (a few hundred ms) is compensated in the grace deadline, measured in S2, so that FR-06 holds at ± 0.5 s. |
| AS-18 | An accepted `BOOT=1` packet produces a `PACKET` message followed by `NODE_REBOOT`. A missing key leaves a booting hub with no accepted packets and the `KEY_FAULT` pattern, not the 780 s silence of an ordinary boot. |

---

## 7. Risks

| ID | Risk | Note |
|---|---|---|
| R-01 | A dead or failed node is only detected as OFFLINE after up to about 780 s. | Accepted by design. The margin may be tuned at test. |
| R-02 | After a hub reset, nodes are UNKNOWN (no alert) until their next packet, up to 360 s for a non-compliant node. | Direct consequence of D-04. Hub resets are reported (`HUB_BOOT`) and should be rare (NFR-08). |
| R-03 | The key is stored in plaintext NVS (AS-13). | Physical access is out of the v1 threat model. |
| R-04 | Wi-Fi and BLE share the S3 radio, which can reduce scan reception. | Mitigated by FR-53. Rechecked in S3 and S7. |
| R-05 | Hub battery life is only 20 to 24 hours in the real-world estimate. | Informational (NFR-11). May drive hardware change. |
| R-06 | A disconnected glove or helmet contact reads NOT_WORN (D-15). | The wearer gets an alert instead of a silent failure, but a broken wire cannot be told from a real removal. Hall sensors in v2 may add detection. |

---

## 8. Change from the draft feature list

- Added: packet `vest_id`, version and type in `flags`, `BOOT`, `LOW_BATT` and `SENSOR_FAULT` flag bits; `HUB_STATUS`; hub diagnostic counters; `reset-counter` command; `age_ms` on uplink messages.
- Removed: the configurable required PPE set (fixed at 5 in v1). The hub event history ring (v1.2, D-19).
- Corrected: duplicated `battery` and `counter` rows in the draft packet table.
- Changed in v1.2: `factory-reset` command (FR-46), node fault handling (FR-17), missing-key fault (FR-30), deep-sleep wake rule, scan watchdog definition, uplink buffer and `hub_seq` rules, `COMPLIANCE` message rule.
