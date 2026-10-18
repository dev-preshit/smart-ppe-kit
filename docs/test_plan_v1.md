# Smart PPE Compliance Firmware: Test Plan (v1)

| Item | Value |
|---|---|
| Document | `docs/test_plan_v1.md` (document revision 1.1) |
| Status | APPROVED (S0 closed) |
| Covers | TC-01 to TC-81 in `docs/traceability_v1.2.md` (58 test cases for 59 requirements in `docs/SRS_v1.2.md`) |
| Companions | `docs/SRS_v1.2.md`, `docs/protocol_spec_v1.1.md`, `docs/state_machines_v1.md`, `docs/s0_findings_v1.md`, `docs/bug_log_template.md` |
| Branch | `feature/s0-statemachines-testplan` |

### Revision history

| Revision | Change |
|---|---|
| 1.0 | First S0 draft against the S0 baseline (tag `s0-docs-done`). TC-52 was blocked on a finding. |
| 1.1 | Aligned with SRS 1.2, protocol spec 1.1 and traceability 1.2. New: TC-17 (FR-17) and TC-46 (FR-46). Rewritten to the new acceptance criteria: TC-06, 08, 09, 11, 15, 16, 21, 22, 24, 27, 28, 30, 32, 41, 50, 51, 52, 74. TC-16 applies to FSR boots only (D-15). TC-52 uses the controlled setup and is no longer blocked. Every Pass line is now the SRS 1.2 acceptance criterion word for word. New hooks for the fault cases. |

## 1. Scope and conventions

- **Pass rule** is the acceptance criterion (AC) of the requirement in SRS 1.2, copied word for word (SC-09 checks it). Findings F-01 to F-20 are resolved in SRS 1.2, so no pass rule is blocked. Where a step depends on an open observation (V12-xx in `docs/s0_findings_v1.md` section 4) the step says which interpretation it uses.
- **Sprint** is the sprint in which the TC first runs and must pass. "Re-run" lists later sprints where the TC is repeated because another part (uplink, Wi-Fi) joins. The sprint plan in section 6 is an **assumption** made for this plan. The owner's sprint breakdown replaces it if it differs.
- **Level:** U host (PC, simulated clock), B bench (hardware, one or two boards), S system (hub, nodes, uplink). R review or inspection. **Method:** T test, I inspection, A analysis, D demonstration (as in the traceability table).
- **Defaults:** `HEARTBEAT_S=360`, `GRACE_S=25`, `SETTLE_MS=0`, `OFFLINE_MARGIN_S=60`, test key (protocol spec Appendix A). Tests that change a default say so. The real key is never used in a test log.
- **Timing method:** logic analyzer sampling at 1 MHz or more (1 µs resolution). Sniffer timestamps are used for on-air timing. Serial log timestamps are used only where ±50 ms is enough.
- **Trials:** counts in the AC are minimums. A failure in any trial is a fail unless the AC gives a percentage.
- **Firmware under test** is identified by the git commit SHA of node, hub and tools, recorded in every result (section 5.3).
- **Devices:** N1 to N5 are the five node boards (left boot, right boot, left glove, right glove, helmet), H1 is the hub, S1 and S2 are two spare ESP32-C3 boards (injector and beacon).

## 2. Equipment and scripts

### 2.1 Equipment

| ID | Item | Used for |
|---|---|---|
| EQ-01 | Logic analyzer, ≥ 8 channels, ≥ 24 MHz | Grace timing, motor pattern, boot level, markers |
| EQ-02 | BLE sniffer (nRF52840 dongle with nRF Sniffer and Wireshark, or equivalent) with microsecond timestamps | Copies, spacing, packet size, flags, 1000-packet capture |
| EQ-03 | Programmable bench supply, 0 to 5 V, 1 mV resolution | Battery sweep, brown-out |
| EQ-04 | Current profiler (Nordic PPK2 or equivalent) | Deep sleep, awake time, NFR-11 |
| EQ-05 | Controllable power switch (relay or MOSFET) driven from the PC | Random power cuts, power-on timing, hub reset |
| EQ-06 | Spare ESP32-C3 "injector" (sketch `tools/injector`) | Crafted advertisements from the PC |
| EQ-07 | Spare ESP32-C3 "beacon" (sketch `tools/beacon`) | One valid packet every 10 s, single copy |
| EQ-08 | Wi-Fi access point with switchable power | Outage tests |
| EQ-09 | Gateway PC (runs `gateway_stub`, later the real gateway and dashboard) | Uplink and dashboard tests |
| EQ-10 | Contact simulator: relay or analog switch board driven by the PC, one channel per contact input | Wear and remove cycles for gloves and helmet. For FSR boots, ADC injection through the hook (section 3) or a divider switched by the board |
| EQ-11 | Oscilloscope or DMM | Voltage and motor cross-checks |
| EQ-12 | USB serial adapters (seven) | Provisioning, logs, hooks |
| EQ-13 | Real vibration motor and driver on the hub (and a dummy load for fault runs) | Motor tests |

### 2.2 Scripts (to be written; owner sprint in brackets)

| ID | Script | Purpose |
|---|---|---|
| SC-01 | `tools/provision.py` | Serial provisioning for node and hub. Reads the key from a file outside the repo [S1] |
| SC-02 | `tools/verify_packets.py` | Independent HMAC-SHA256 verifier over a sniffer capture. Counter, copy and flag checks [S1] |
| SC-03 | `tools/gen_vectors.py` | Appendix A vectors, 136 bit-flip variants, forged and replay sets [S1] |
| SC-04 | `tools/inject.py` | Sends hex advertisements through the injector with a PC timestamp [S3] |
| SC-05 | `tools/gateway_stub.py` | HTTP receiver. Token check, arrival timestamp, schema validation, de-duplication, configurable delay and failure, JSONL log [S7, minimal in S3] |
| SC-06 | `tools/logger.py` | Timestamped serial capture to `test_logs/` [S1] |
| SC-07 | `tools/analyze_timing.py` | Logic analyzer CSV to grace, copy spacing, motor ON/OFF [S2] |
| SC-08 | `tools/powercut.py` | Random power cuts with a seeded random generator [S4] |
| SC-09 | `tools/check_traceability.py` | NFR-12: SRS to traceability to test plan to state machine check. Also compares every Pass line with the SRS acceptance criterion and fails on a pending marker or a reference to an old document name [S1] |
| SC-10 | `tools/secret_scan.sh` | `gitleaks` and `git grep` for the real key in all commits, in hex, spaced bytes, C array and base64 forms [S1] |
| SC-11 | `tools/soak_monitor.py` | Heap, reset count and uptime tracking over 24 h [S7] |
| SC-12 | `tools/drive_contacts.py` | Runs wear and remove cycles through EQ-10 [S2] |
| SC-13 | `make test` in `test/host/` | Runs all host unit tests (section 5.1) [S1] |
| SC-14 | `tools/schema/uplink.schema.json` | Uplink message schema with the `detail` object per type (protocol spec 10.1) [S1] |

## 3. Test hooks

Compiled only with `-DPPE_TEST_HOOKS`. The release build is checked in TC-78 to contain none of them.

| Device | Hook | Effect |
|---|---|---|
| Node | `adc <n>` / `adc fail` / `adc ok` | Replaces the ADC count, forces an ADC read failure, or clears it. The flag is kept in RTC memory |
| Node | `marker` | A GPIO pulse at the candidate change, at grace expiry and at the first copy start, for the logic analyzer |
| Node | `tx` | Sends one logical packet at once (bypasses grace), for counter and power-cut tests |
| Node | `setcounter <n>` | Sets `counter` (Appendix A vector tests) |
| Node | `nvslog` | Prints every NVS write with the key and value |
| Node | `skiptx <n>` | Suppresses all copies of the next n logical packets |
| Node | `show` | Prints the stored configuration (no key) |
| Node | `fault <nvs-write\|ble>` | Makes the next counter-block NVS write fail, or the BLE stack fail to start twice |
| Node | `awake <s>` | Keeps the node awake for s seconds (serial commands, hook settings) |
| Hub | `inject <hex>` | Feeds a raw advertisement into the receive pipeline at full speed |
| Hub | `stall <n>` | Forces a scan stall, with n consecutive failed restarts |
| Hub | `fault <wdt\|nvs-read\|nvs-write\|registry\|key\|guard>` | Injects a watchdog hang, an NVS read failure, an NVS write failure, an unreadable registry, a missing key, or a hang of the pattern task while the motor is ON (guard trip) |
| Hub | `dump` | Prints registry, `last_counter`, `last_seen`, counters, queue fill |
| Hub | `time` | Prints the monotonic time with every accepted packet and every state change |

## 4. Test cases

Format: ID, title (requirement) · level / method · sprint. "Pass" is the SRS acceptance criterion.

### 4.1 Node (FR-01 to FR-16)

### TC-01 Identity from NVS, same binary (FR-01) · B / T · S4
- **Setup:** One node binary. Two provisioned-capable C3 boards (N1, N2) and a third blank board. Sniffer, test key.
- **Steps:** 1. Flash the same `.bin` on N1 and N2. 2. Provision N1 as `node_id 0x01`, N2 as `0x02` (SC-01). 3. Power-cycle each and capture its BOOT event. 4. Decode `node_id` from the capture. 5. Flash the blank board, leave it unprovisioned, capture for 10 min.
- **Tools:** SC-01, EQ-02, SC-02, SC-06.
- **Pass:** One binary flashed on two boards provisioned as `0x01` and `0x02` sends packets with the matching `node_id`. An unprovisioned board transmits 0 packets in 10 minutes.

### TC-02 Glove sensing (FR-02) · B / T · S2
- **Setup:** Glove node (N3, then N4). EQ-10 relay across the strip input. Candidate state printed on serial. `SETTLE_MS=0`.
- **Steps:** 1. Run 20 connect and disconnect cycles (SC-12), each state held 2 s. 2. Compare the logged candidate with the relay state after each change.
- **Tools:** EQ-10, SC-12, SC-06.
- **Pass:** 20 connect/disconnect cycles on each glove node: candidate state matches contact in 100% of cycles (debug log).

### TC-03 FSR hysteresis (FR-03, NFR-13) · U/B / T · S2
- **Setup:** Host: the FSR conversion function with the injected sequence. Bench: N1 and N2 with `adc <n>`.
- **Steps:** 1. Feed 0, 88, 89, 800, 1536, 1537, 4095, 1536, 89, 88 and log the state after each. 2. Power on with a count of 800 and read the state. 3. Repeat steps 1 and 2 on N1 and N2.
- **Tools:** SC-13, node `adc` hook, SC-06.
- **Pass:** Injected count sequence 0, 88, 89, 800, 1536, 1537, 4095, 1536, 89, 88: state changes only at 1537 (to WORN) and 88 (to NOT_WORN). A power-on reading of 800 gives NOT_WORN.

### TC-04 Helmet two-factor (FR-04) · B / T · S2
- **Setup:** Helmet node N5 with the crown switch and strap contact on two EQ-10 channels.
- **Steps:** 1. Apply all 4 combinations (open/open, closed/open, open/closed, closed/closed), each held 2 s. 2. Read the candidate state for each.
- **Tools:** EQ-10, SC-12.
- **Pass:** All 4 input combinations tested: only both-closed gives WORN.

### TC-05 Settle time (FR-05) · B / T · S2
- **Setup:** Any node with the `marker` hook. Logic analyzer on the sensor input and the marker.
- **Steps:** 1. `SETTLE_MS=0`: 20 changes, measure change to candidate marker. 2. `SETTLE_MS=500`: 10 pulses of 400 ms, check there is no candidate marker. 3. `SETTLE_MS=500`: 10 changes held 600 ms, check there is a marker.
- **Tools:** EQ-01, EQ-10, SC-07.
- **Pass:** `SETTLE_MS=0`: candidate within one poll (≤ 50 ms). `SETTLE_MS=500`: a 400 ms pulse gives no candidate, a 600 ms change does.

### TC-06 Grace period (FR-06) · B / T · S2 (marker, scenarios a to d), S4 (on air, scenarios a to e)
- **Setup:** Node starting from deep sleep, so the wake latency is included (AS-17). Logic analyzer on the sensor input and the first-copy marker. In S4 the sniffer timestamps replace the marker. Scenario (e) needs a running transmission sequence, so it runs in S4 only.
- **Steps:** (a) Change and hold 25 s: 10 trials. (b) Change, revert at 10 s. (c) Change, revert at 10 s, change again at 12 s. (d) Provision `GRACE_S=10`, change and hold. (e) Provision `GRACE_S=1`, start a sequence with the `tx` hook, change the sensor 0.5 s into it, and check on the sniffer trace that the new EVENT starts after that sequence ends and no copies interleave.
- **Tools:** EQ-01, EQ-02, EQ-10, SC-12, SC-07, node `tx` hook.
- **Pass:** (a) Change held 25 s: exactly one event at 25.0 ± 0.5 s. (b) Reverted at 10 s: 0 packets. (c) Change, revert at 10 s, change again at 12 s: event at 37 ± 0.5 s. (d) `GRACE_S=10`: event at 10 ± 0.5 s. (e) `GRACE_S=1` and a change 0.5 s into a running sequence: the new EVENT starts after that sequence ends and the copies never interleave.

### TC-07 Event transmission (FR-07) · B / T · S4
- **Setup:** One node of each type (boot, glove, helmet). Sniffer, verifier.
- **Steps:** 1. Run 20 wear and 20 remove cycles with a hold longer than grace. 2. Decode each capture: count logical packets, state, counter.
- **Tools:** EQ-02, EQ-10, SC-12, SC-02.
- **Pass:** 20 wear and 20 remove cycles: each produces one logical EVENT with the correct state and counter + 1.

### TC-08 Boot behavior (FR-08) · B / T · S4
- **Setup:** Node, sniffer, EQ-05 for power-on. Logic analyzer on the power switch and the sniffer sync.
- **Steps:** (a) Power on with the sensor worn, then not worn, 3 times each, for a boot, a glove and the helmet. Check there is no packet before 25 s, then the event time, the `BOOT` flag and the state. (b) Power on, change the sensor 10 s later, and measure from the change to the `BOOT=1` EVENT and check its state. Also check that the packets after the `BOOT=1` EVENT have `BOOT=0` (including after deep-sleep wakes).
- **Tools:** EQ-05, EQ-02, SC-02, EQ-10.
- **Pass:** (a) Power-on worn and not worn: no packet before 25 s, then exactly one logical EVENT with `BOOT=1` and the correct state at 25.0 ± 0.5 s. (b) Candidate changed 10 s after power-on: the `BOOT=1` EVENT is sent 25 ± 0.5 s after the change, with the new state.

### TC-09 Heartbeat (FR-09) · B / T · S4
- **Setup:** Node with default `HEARTBEAT_S=360`, sniffer logging for about 1 h. Development runs may use `HEARTBEAT_S=60`. Acceptance of the first two steps uses 360.
- **Steps:** 1. Capture 10 consecutive heartbeats and compute the intervals (first copy to first copy). 2. Trigger an event and measure the time to the next heartbeat. 3. Provision `HEARTBEAT_S=60`, change the sensor 50 s after the last transmission, and check for a heartbeat with the old confirmed state, then the EVENT. 4. RTC clock accuracy: compare the node-timed interval with the sniffer time base over the 10 intervals. If the error is above ±5% (AS-15), calibrate the RTC slow clock and repeat steps 1 and 2.
- **Tools:** EQ-02, EQ-10, SC-02.
- **Pass:** 10 consecutive heartbeats: every interval ≤ 378 s and mean 360 ± 18 s. After an event the next heartbeat is 360 ± 18 s later. With `HEARTBEAT_S=60` and a change 50 s after the last transmission: a heartbeat with the old confirmed state at 60 ± 3 s, then the EVENT at 75 ± 0.5 s.

### TC-10 5-copy retransmission (FR-10) · B / T · S4
- **Setup:** Node, sniffer with microsecond timestamps.
- **Steps:** 1. Capture 20 events and 20 heartbeats (heartbeat runs may use `HEARTBEAT_S=60`). 2. For each logical packet count copies, compare bytes, measure start-to-start spacing. 3. Check for any copy in the next 10 s.
- **Tools:** EQ-02, SC-02, SC-07.
- **Pass:** Sniffer capture of 20 events and 20 heartbeats: exactly 5 identical copies each at 500 ± 50 ms, and no further copy in the following 10 s.

### TC-11 Counter, NVS block and deep-sleep wake (FR-11, NFR-06) · B / T · S4
- **Setup:** Bench: node with `tx`, `nvslog` and EQ-05. Sniffer. Traceability 1.2 sets this TC at level B. A host test of the counter logic with a fake NVS is run as a supplementary unit test (step 5).
- **Steps:** 1. Capture 20 logical packets and check the counters and the copies. 2. Force 100 resets and log the first counter after each. 3. Send 640 or more packets with `tx` and count the NVS writes. 4. With `HEARTBEAT_S=60`, run at least 20 heartbeat wakes (about 20 min): `nvslog` shows no NVS write, and `BOOT=1` appears only on the first logical packet after a reset. 5. Supplementary host test: 1,000,000 packets with a fake NVS, and the case near the counter limit.
- **Tools:** SC-13, EQ-05, EQ-02, SC-02, node `nvslog`.
- **Pass:** Consecutive logical packets differ by 1 (copies equal). After 100 forced resets the counter never repeats or decreases. At most 1 NVS write per 64 packets (NVS write log) across at least 20 heartbeat wakes, and `BOOT=1` appears only on the first logical packet after a reset.

### TC-12 HMAC generation (FR-12) · U/B / T · S1 (host), S4 (bench)
- **Setup:** Host with SC-03 vectors. Bench: node with the test key, `setcounter`, sniffer.
- **Steps:** 1. Host: build A.1 and A.2 and compare all 21 and 17 bytes. 2. Bench: send A.1 (set the counter to 258) and compare on air. 3. Capture 1000 packets and run the independent verifier.
- **Tools:** SC-03, SC-13, EQ-02, SC-02.
- **Pass:** Node output matches Appendix A vectors with the test key. An independent Python verifier accepts 1000 of 1000 captured packets.

### TC-13 Battery report (FR-13) · B / T · S4
- **Setup:** Node powered from EQ-03 on its battery input. Sniffer.
- **Steps:** 1. Set 4.2, 3.9, 3.6, 3.3 V and capture a packet at each. 2. Decode `battery` and `LOW_BATT`. 3. Force `adc fail` for the battery channel and capture.
- **Tools:** EQ-03, EQ-02, SC-02, `adc fail`.
- **Pass:** Bench supply at 4.2, 3.9, 3.6, 3.3 V: reported percent is non-increasing and `LOW_BATT` is set at ≤ 20%. With the ADC read failing, battery = `0xFF`.

### TC-14 Deep sleep (FR-14) · B / T · S4
- **Setup:** Node on EQ-04. Event trigger through EQ-10.
- **Steps:** 1. Record 10 min of idle current. 2. Trigger 5 events and record the profile. 3. Measure awake time (current above the sleep level) from change to return to sleep.
- **Tools:** EQ-04, EQ-10.
- **Pass:** Current profile shows deep sleep between activity. Awake time per event ≤ `GRACE_S` + 4 s.
- **Note:** The sleep current level is recorded (it feeds TC-80). Sleep is accepted as flat current below 1 mA on the board under test.

### TC-15 Wake level flip and loop guard (FR-15) · B / T · S4
- **Setup:** Node in deep sleep (confirmed by the profiler or log). EQ-10 for gloves and helmet, `adc` hook for FSR boots. Wake cause and wake count logged on serial and visible on EQ-04.
- **Steps:** 1. Run 20 wear/remove cycles, each change at least 30 s after the previous one so the node sleeps in between, and check each change produces an event. 2. FSR boot: hold a count in the dead band for 10 s and log the wakes. 3. Helmet: close only one contact for 10 s and log the wakes. 4. In both cases check at most 2 consecutive GPIO wakes before the timer-poll wake. 5. Then make a real change and record the time to the first copy. Derived limit: at most `GRACE_S` + `SETTLE_MS` + 1.5 s (poll period 1 s plus the ±0.5 s of FR-06), which leaves at least 1.5 s of the 3 s NFR-01 allowance for the hub. TC-70 checks the full latency.
- **Tools:** EQ-10, SC-12, EQ-04, EQ-02, `adc`.
- **Pass:** 20 consecutive wear/remove cycles each wake the node from deep sleep with no missed change. An FSR count held in the dead band, and a helmet with only one contact closed, for 10 s each: at most 2 consecutive GPIO wakes before the node switches to timer-poll, and a real change is still detected within the NFR-01 budget.

### TC-16 Sensor fault, FSR nodes only (FR-16, NFR-13) · B / T · S2 (marker), S4 (on air)
- **Setup:** FSR boot nodes N1 and N2 with the `adc fail` and `adc ok` hooks (the flag is kept in RTC memory so it survives deep sleep). Marker and sniffer. Gloves and helmet are not applicable (D-15).
- **Steps:** 1. Node confirmed WORN: set `adc fail`. The reference time is the first failed sample (logged by the node). Measure the time to the first copy and decode `state` and `SENSOR_FAULT`. 2. Node already confirmed NOT_WORN: set `adc fail` and repeat the measurement. 3. Set `adc ok` and measure the time to the EVENT with `SENSOR_FAULT=0`. 4. Gloves and helmet: record `N/A (D-15)` in `result.json`. As a demonstration, open a glove or helmet contact and check the node reads NOT_WORN and the hub starts the alert (R-06, TC-29 setup).
- **Tools:** EQ-01, EQ-02, SC-07, `adc fail`, `adc ok`.
- **Pass:** FSR node with the ADC read forced to fail, even when already NOT_WORN: a packet with `SENSOR_FAULT=1` is sent 25 ± 0.5 s later. Clearing the fault sends an EVENT with `SENSOR_FAULT=0` 25 ± 0.5 s later. Gloves and helmet: not applicable (a disconnected contact reads NOT_WORN, see R-06).

### TC-17 Node faults, NODE_FAULT (FR-17) · B / T · S4 (node side), re-run S5 (hub OFFLINE and alert)
- **Setup:** Node N3 with the hooks `fault nvs-write`, `fault ble`, `setcounter` and `tx`. Sniffer and serial log. From S5 the hub H1 with the node registered and the motor on EQ-01.
- **Steps:** For each fault in turn: (a) counter limit: set the counter just below `0xFFFFFFF0` and send packets with `tx` until the limit. (b) NVS block write failure: arm `fault nvs-write`, then force a block extension (send 64 packets with `tx`) and, in a second run, a cold boot. (c) BLE start failure: arm `fault ble` (two failed starts). Then: 1. Check the cause on serial. 2. Capture 60 s on the sniffer: 0 packets after the fault. 3. Record the last counter before the fault and, after a reset (cases b and c), the first counter after: it must never repeat or decrease. 4. S5: check the hub declares the node OFFLINE within 785 s of its last packet and the motor alert is active. 5. Check a reset clears (b) and (c) and that (a) returns until the node is re-provisioned.
- **Tools:** node hooks, EQ-02, SC-02, SC-06, hub `time`, EQ-01.
- **Pass:** With each fault forced in turn: 0 packets are sent after the fault, the cause is on serial, the node is OFFLINE at the hub within 785 s and the motor alert is active. The counter log never repeats a value.

### 4.2 Hub (FR-20 to FR-32)

### TC-20 Continuous scan (FR-20) · S / T · S3, re-run S7
- **Setup:** H1 with the five nodes (or the injector) and the beacon (EQ-07): one valid single-copy packet every 10 s, test key.
- **Steps:** 1. Check all 5 nodes are received (`dump` accepted per node). 2. Run the beacon for 1 h and read the hub counters (`accepted` for the beacon node). 3. Compute the received percentage out of 360.
- **Tools:** EQ-07, `dump`, SC-06.
- **Pass:** The hub receives packets from all 5 nodes. A test beacon every 10 s for 1 h is received ≥ 99%.
- **Re-run S7:** with Wi-Fi connected (coexistence recheck).

### TC-21 Scan watchdog (FR-21) · B / T · S3
- **Setup:** H1 with the `stall` hook and the beacon running. For step 3 an RF-quiet place (or a shielded box) with all nodes and the beacon off.
- **Steps:** 1. `stall 1`: measure the time until a beacon packet is accepted again. 2. `stall 3`: check the reboot (reset reason in the log). 3. Leave the hub 10 min with no BLE traffic at all and the scanner active: check no scan restart and no reboot. 4. Supplementary, no SRS pass rule: `scan_quiet_60s` rises by about 1 per minute and is only logged.
- **Tools:** `stall`, EQ-07, SC-06, `dump`.
- **Pass:** Forced scan stall (test hook): the scan resumes within 10 s and packets are received again. Three forced consecutive failures cause a reboot. With no BLE traffic at all for 10 min and the scanner active: no restart and no reboot.

### TC-22 Receive filter order (FR-22) · U/B / T · S3
- **Setup:** Host: the pipeline function. Bench: H1 with the injector (SC-04).
- **Steps:** 1. Inject 20 packets of each defect: bad length (20 and 22 bytes), bad AD header, wrong version (`00b`, `10b`, `11b`), wrong vest, unknown node (`0x00`, `0x06`). 2. Inject 20 packets with a valid HMAC and `state` `0x02` (made with SC-03). 3. Compare each drop counter with the injected number. 4. Check the order: a packet with two defects increments only the first matching counter.
- **Tools:** EQ-06, SC-04, SC-03, `dump`, SC-13.
- **Pass:** Crafted packets with a bad length, bad AD header, wrong version, wrong vest, unknown node: 0 accepted. A packet with a valid HMAC and `state` 0x02: 0 accepted. The matching drop counter increments for each.

### TC-23 HMAC verify (FR-23) · U/B / T · S3
- **Setup:** Host with SC-03. Bench: H1 with the injector.
- **Steps:** 1. Inject the valid A.1 vector. 2. Inject the 136 single-bit-flip variants (17 bytes × 8 bits). 3. After each variant check `last_counter` with `dump`.
- **Tools:** SC-03, EQ-06, SC-04, `dump`, SC-13.
- **Pass:** The valid vector is accepted. All 136 single-bit-flipped variants of bytes 0 to 16: 0 accepted. `last_counter` unchanged after each.

### TC-24 Counter rule and persistence (FR-24) · U/B / T · S3, re-run S7
- **Setup:** Host with fake NVS. Bench: H1, injector, 5 registered nodes.
- **Steps:** 1. Send 5 copies of one packet: check accepted 1, dup 4 and `last_seen` changed once. 2. Replay 100 older captured packets after newer ones: check `replay_count = 100` and that `COUNTER_ANOMALY` is reported (rate limited to once per node per 60 s, protocol spec 6.2, so check at least one per node per 60 s window). 3. Wait at least 2 s after the last copy, reset the hub, and replay the last accepted packet. S7: check `COUNTER_ANOMALY` at the gateway.
- **Tools:** EQ-06, SC-04, SC-03, `dump`, `time`.
- **Pass:** 5 copies of one packet: 1 accepted, 4 dropped as duplicates, `last_seen` updated once. 100 old captured packets replayed after newer ones: 0 accepted and anomalies reported. After a hub reset (at least 2 s after the last copy) the last accepted packet replayed is rejected.

### TC-25 Registry (FR-25) · B / T · S3
- **Setup:** H1, injector, 5 registered nodes.
- **Steps:** 1. Send packets for all 5 nodes and `dump`. 2. Reset the hub and `dump`.
- **Tools:** EQ-06, EQ-05, `dump`.
- **Pass:** After a hub reset `last_counter` values are retained and all states are UNKNOWN.

### TC-26 Node state machine (FR-26) · U / T · S5
- **Setup:** Host: node FSM with a fake clock.
- **Steps:** 1. Run a scripted sequence that covers every transition H-01 to H-13, including OFFLINE to WORN and UNKNOWN to NOT_WORN. 2. Compare the state and the emitted messages with the table in spec 8.2.
- **Tools:** SC-13.
- **Pass:** Scripted packet sequences produce exactly the transitions in the spec table, including OFFLINE to WORN and UNKNOWN to NOT_WORN.

### TC-27 Offline detection (FR-27) · S / T · S5
- **Setup:** H1 with nodes (or the injector acting as a node), hub `time` log. Node and hub provisioned with the same `HEARTBEAT_S` (FR-41).
- **Steps:** 1. Power off a node after its last packet and measure the time to OFFLINE at the default margin (780 s). 2. Repeat with `OFFLINE_MARGIN_S=0`. 3. Drop exactly one heartbeat (all 5 copies) with `skiptx 1` and check there is no OFFLINE.
- **Tools:** `time`, `skiptx`, EQ-05.
- **Pass:** Node powered off: OFFLINE between 780 s and 785 s after its last packet at the default margin. With margin 0: between 720 s and 725 s. Dropping exactly one heartbeat (all 5 copies) does NOT cause OFFLINE.

### TC-28 Compliance engine (FR-28, NFR-13) · U / T · S5, re-run S7
- **Setup:** Host: compliance engine. S7: gateway stub.
- **Steps:** 1. All 5 nodes WORN. 2. For each node in turn set NOT_WORN, OFFLINE and UNKNOWN. 3. Count the `COMPLIANCE` messages: exactly one per vest-level change, with the new `state` and `nodes_not_ok`. 4. Reset the hub and check no `COMPLIANCE` message is queued at boot. S7: repeat steps 3 and 4 at the gateway.
- **Tools:** SC-13, SC-05.
- **Pass:** All 5 WORN: COMPLIANT. For each node in turn, NOT_WORN, OFFLINE and UNKNOWN each give NON_COMPLIANT. Each vest-level change sends exactly one `COMPLIANCE` message with the new state, and none is sent at hub boot.

### TC-29 Motor alert (FR-29) · B / T · S5
- **Setup:** H1 with the real motor driver (EQ-13). Logic analyzer on the motor pin and on the injector TX marker.
- **Steps:** 1. Inject a NOT_WORN packet: time from the on-air packet to the motor pin high. 2. Record 60 s of the pattern. 3. Inject a WORN packet from the last offender: time to the motor pin low. 4. Reset the hub with all nodes UNKNOWN: check no motor activity. 5. Repeat step 1 with an OFFLINE trigger.
- **Tools:** EQ-01, EQ-06, EQ-13, SC-07.
- **Pass:** ON 300 ± 30 ms and OFF 700 ± 30 ms over 60 s (logic analyzer). Starts ≤ 200 ms after the accepted NOT_WORN packet. Stops ≤ 1 s after the last offending node recovers. No alert with all nodes UNKNOWN after hub boot.

### TC-30 Hub fault handling (FR-30, NFR-13) · B / T · S5, re-run S7
- **Setup:** H1 with the fault hooks, logic analyzer on the motor pin, serial log. From S7 the gateway stub.
- **Steps:** 1. Inject each fault and check the recovery or reboot, the motor trace (ON ≤ 400 ms) and the `HUB_FAULT` code: scan stall (`stall 3`, `SCAN_STALL`), `fault wdt` (`WDT_RESET`), `fault nvs-read` (`NVS_READ`), `fault nvs-write` (`NVS_WRITE`), `fault guard` (`GUARD`). The reboot cases report the code after boot. 2. Missing key (`fault key`): time from boot to the `NO_KEY` serial line, then record 5 periods of the motor pin and measure ON, OFF (×3) and the low time after the third ON. Check the alert pattern never runs while the `KEY_FAULT` pattern does, including after the nodes would be OFFLINE. 3. Unreadable registry (`fault registry`): check all nodes OFFLINE within 785 s of boot, the alert active and `HUB_FAULT` (`REGISTRY`). Serial log in S5, gateway in S7.
- **Tools:** hub hooks, EQ-01, SC-05, SC-06.
- **Pass:** Each fault injected: the hub recovers or reboots, the motor is never continuously on (ON ≤ 400 ms), and `HUB_FAULT` with the right code is reported. Missing key: `HUB_FAULT` (`NO_KEY`) within 2 s of boot (serial log when no uplink) and the `KEY_FAULT` pattern with ON 100 ± 20 ms, OFF 100 ± 20 ms (×3) then OFF 1700 ± 100 ms. Unreadable registry: all nodes OFFLINE within 785 s of boot and the motor alert is active.

### TC-31 Hub boot behavior (FR-31) · S / T · S5, re-run S7
- **Setup:** H1 with running nodes, gateway stub in S7.
- **Steps:** 1. Reset the hub (EQ-05). 2. `dump` the states and watch the motor. 3. Check `HUB_BOOT` arrives. 4. With one node switched off, time its OFFLINE from boot.
- **Tools:** EQ-05, `dump`, `time`, SC-05.
- **Pass:** After reset: states UNKNOWN, no alert, `HUB_BOOT` received. A silent node is OFFLINE 780 ± 5 s after boot.

### TC-32 Diagnostic counters (FR-32) · U/B / T · S3, re-run S7
- **Setup:** Host and H1 with the injector.
- **Steps:** 1. Inject known numbers of each kind (for example 10 bad length, 7 wrong version, 12 wrong vest, 9 unknown node, 15 bad HMAC, 6 valid HMAC with invalid `state`, 20 duplicates, 8 replays, 30 accepted). 2. Compare the global and per-node counters in `dump` with the injected numbers, including `drop_state`. 3. S7: compare the counters in `HUB_STATUS` (`diag` and per node).
- **Tools:** SC-03, SC-04, `dump`, SC-05.
- **Pass:** After a crafted-packet run the counters equal the number of injected packets of each kind. A packet with a valid HMAC and an invalid `state` increments `drop_state`.

### 4.3 Provisioning (FR-40 to FR-45)

### TC-40 Node provisioning (FR-40) · B / T · S6
- **Setup:** One node board, SC-01, the key file outside the repo.
- **Steps:** 1. Write all fields (`node_id`, `vest_id`, key, `SETTLE_MS`, `GRACE_S`, `HEARTBEAT_S`, counter baseline). 2. Power-cycle and `show`. 3. Write out-of-range values one at a time (`node_id` 0 and 6, `vest_id` 0 and 255, `SETTLE_MS` 1001, `GRACE_S` 0 and 121, `HEARTBEAT_S` 59 and 3601, key of 31 and 33 bytes) and `show` before and after.
- **Tools:** SC-01, `show`.
- **Pass:** Values persist across power cycles. An out-of-range value is rejected with an error and no change.

### TC-41 Hub provisioning (FR-41) · B / T · S6
- **Setup:** H1, SC-01.
- **Steps:** 1. Write `vest_id`, key, Wi-Fi credentials, gateway IP and port, token, `HEARTBEAT_S` and `OFFLINE_MARGIN_S`. 2. Power-cycle and `dump`. 3. Write out-of-range values (`vest_id` 0 and 255, `HEARTBEAT_S` 59 and 3601, `OFFLINE_MARGIN_S` 301, bad IP, empty token). 4. Provision `HEARTBEAT_S=60` with margin 0 and then margin 30, and measure the OFFLINE time of a silent node. 5. Inject valid-HMAC packets from node ids `0x00` and `0x06` and from ids 1 to 5.
- **Tools:** SC-01, EQ-06, `dump`, `time`.
- **Pass:** Values persist across power cycles. Out-of-range values rejected. `OFFLINE_TIMEOUT_S` follows the provisioned `HEARTBEAT_S` (a hub set to 60 s declares OFFLINE at 120 s plus margin). The hub accepts packets only from node ids 1 to 5.

### TC-42 Key handling (FR-42, NFR-04) · R / I,T · S1 (setup), every sprint close, S9 (final)
- **Setup:** The real key file kept outside the repository. SC-10.
- **Steps:** 1. Run the scan over every commit on the branch (`git log -p --all`) for the real key in hex, spaced bytes, C array and base64 forms. 2. Check `.gitignore` rules and that only the placeholder example exists. 3. Build node and hub from a clean checkout with no key present.
- **Tools:** SC-10, Arduino IDE or CLI.
- **Pass:** A secret scan (`git grep`/gitleaks) over all commits on the branch finds 0 occurrences of the real key. Firmware builds with no key in source.

### TC-43 `reset-counter` command (FR-43) · S / T · S6, re-run S7
- **Setup:** H1, a node, gateway stub in S7.
- **Steps:** 1. Re-provision the node with baseline 0 (counter restarts low). 2. Check the hub drops its packets and flags `COUNTER_ANOMALY`. 3. Run `reset-counter <node_id>`. 4. Check the next packet is accepted and `COUNTER_RESET` is reported.
- **Tools:** SC-01, `dump`, SC-05.
- **Pass:** A replaced node (counter 1) is dropped and flagged until the command is run, then accepted. `COUNTER_RESET` is reported.

### TC-44 Configurable timeouts (FR-44) · B / T · S6
- **Setup:** Node and hub with parameter changes, sniffer, hub `time`.
- **Steps:** 1. `GRACE_S=10`: measure 5 events. 2. `HEARTBEAT_S=60`: measure 10 intervals. 3. `OFFLINE_MARGIN_S=0`: measure the OFFLINE time of a silent node.
- **Tools:** EQ-02, `time`, SC-07.
- **Pass:** `GRACE_S=10`, `HEARTBEAT_S=60`, `OFFLINE_MARGIN_S=0` each take effect and are measured at the configured value ± 5%.

### TC-45 Serial-only provisioning (FR-45) · S / T,I · S6
- **Setup:** A running node and hub, a phone or PC with a BLE scanner, a port scanner, the injector.
- **Steps:** 1. BLE scan: no connectable advertisement, no GATT service from the node, no BLE advertisement from the hub. 2. Wi-Fi: the hub is a station only (no AP), scan its IP for open ports. 3. Inject valid-HMAC packets with odd field values and check the configuration is unchanged (`show`, `dump`). 4. Code review: no write to configuration from any radio callback.
- **Tools:** BLE scanner, nmap, EQ-06, review checklist.
- **Pass:** A BLE/Wi-Fi scan of a running node and hub shows no configuration service. No packet changes any configuration value.

### TC-46 Factory reset (FR-46) · S / T · S6
- **Setup:** Node N3 and hub H1, both provisioned and running. SC-01, sniffer, logic analyzer on the motor pin. A sleeping node can take serial commands only while awake, so power-cycle it and send the command inside the boot grace window (V12-02).
- **Steps:** 1. Send `factory-reset` without `CONFIRM` to the node and to the hub, and compare `show` and `dump` before and after: nothing changes and the node keeps transmitting. 2. Send `factory-reset CONFIRM` to the node and capture 10 min: 0 packets and `show` empty. 3. Send `factory-reset CONFIRM` to the hub: after the reboot check `HUB_FAULT` (`NO_KEY`) on serial, the `KEY_FAULT` pattern on the motor pin, and `dump` with the registry and counters erased. 4. Re-provision both with SC-01, run `reset-counter` on the hub for the node, and check normal operation (events accepted, no `KEY_FAULT` pattern).
- **Tools:** SC-01, EQ-02, EQ-01, `show`, `dump`.
- **Pass:** After the command and a reboot: a node sends 0 packets in 10 min and a hub runs the `KEY_FAULT` pattern. Re-provisioning restores normal operation. The command without `CONFIRM` changes nothing.

### 4.4 Uplink (FR-50 to FR-54)

### TC-50 Forwarding (FR-50) · S / T · S7
- **Setup:** H1, gateway stub (SC-05) with a `lose-response` option, injector and fault hooks.
- **Steps:** 1. Generate 50 mixed events (`PACKET` events and heartbeats, `NODE_OFFLINE`, `NODE_REBOOT`, `COMPLIANCE`, `COUNTER_ANOMALY`, `COUNTER_RESET`, `HUB_BOOT`, `HUB_FAULT`, `HUB_STATUS`). 2. Compare the gateway log with the hub log by `hub_seq`. 3. Set the stub to store a message and drop the response once: check the hub retries and the message is stored once. 4. Reset the hub once during the run and check the gateway separates the boots (`HUB_BOOT`, `hub_seq` restarting at 0).
- **Tools:** EQ-06, SC-05, hub hooks.
- **Pass:** 50 mixed events: each appears exactly once at the gateway with correct fields. A POST whose response is lost and is retried is stored once.

### TC-51 Message format (FR-51) · U/S / T · S7 (schema ready in S1)
- **Setup:** `tools/schema/uplink.schema.json` (SC-14), gateway stub with validation. Host serializer test.
- **Steps:** 1. Host: serialize every `msg_type` with its `detail` object (protocol spec 10.1) and validate. 2. System: validate every message of TC-50 and TC-52. 3. Check `hub_seq` is gap-free within each boot in a loss-free run (reset the hub once and check each boot separately).
- **Tools:** SC-14, SC-05, SC-13.
- **Pass:** Every received message validates against the schema. `hub_seq` is gap-free within a boot in a loss-free run.

### TC-52 Buffered retry (FR-52) · S / T · S7
- **Setup:** Controlled setup: all nodes at `HEARTBEAT_S=3600` and the hub provisioned with `HEARTBEAT_S=3600` as well (FR-41), so no heartbeat or `NODE_OFFLINE` message enters the buffer. H1 with EQ-08 (AP off) or the gateway stub set to fail. Injector for the events, with another node held NOT_WORN so no `COMPLIANCE` message is produced by the events.
- **Steps:** 1. Uplink down for 30 min with 60 events. 2. Restore the uplink and check the order and that all 60 events are delivered. 3. Repeat with 120 events and compute the expected `dropped` from the hub log (messages generated during the outage, excluding superseded `HUB_STATUS`, minus 100). 4. Read `dropped` and `superseded` from a `HUB_STATUS` generated after the restore (a `HUB_STATUS` delivered earlier carries the counts of its generation time, V12-08) and compare them with the missing `hub_seq` values. 5. Check the backoff (1 s doubling to 30 s, reset on success) and the 2 s HTTP timeout in the hub log.
- **Tools:** EQ-08, SC-05, EQ-06.
- **Pass:** After the uplink is restored, delivered messages are in `hub_seq` order, the missing messages are exactly the oldest ones, and the number of missing `hub_seq` values equals `dropped` + `superseded`. Controlled setup (nodes at `HEARTBEAT_S=3600`, Wi-Fi off 30 min): 60 events are all delivered. With 120 events, `dropped` equals the messages generated during the outage (excluding superseded `HUB_STATUS`) minus 100.

### TC-53 Wi-Fi non-blocking (FR-53) · S / T · S7
- **Setup:** H1 with the AP absent or the wrong password (association failing continuously). Baselines from TC-29 (S5) and TC-20 (S3).
- **Steps:** 1. Repeat the TC-29 motor timing. 2. Repeat the 1 h beacon test of TC-20. 3. Compare with the baselines.
- **Tools:** EQ-01, EQ-07, EQ-06.
- **Pass:** With Wi-Fi association failing continuously, FR-29 timings and FR-20 reception are unchanged.

### TC-54 `HUB_STATUS` (FR-54) · S / T · S7
- **Setup:** H1, gateway stub.
- **Steps:** 1. Log the arrival time of 11 consecutive `HUB_STATUS`. 2. Compute 10 intervals. 3. Check the content fields (`uptime_s`, `nodes`, `diag`, `buffer_fill`, `dropped`, `superseded`, protocol spec 10.1).
- **Tools:** SC-05.
- **Pass:** 10 consecutive `HUB_STATUS` intervals are 60 ± 3 s.

### 4.5 Dashboard (FR-60 to FR-63)

### TC-60 Live status (FR-60) · S / T · S8
- **Setup:** Dashboard on the gateway PC, SC-05 posting injected data for 5 nodes.
- **Steps:** 1. Post states, battery, low battery, last seen, offline for each node. 2. Compare the displayed fields. 3. Measure time from arrival to display.
- **Tools:** SC-05, browser.
- **Pass:** Each field matches the injected data for all 5 nodes, and updates ≤ 5 s after a message arrives.

### TC-61 Arrival timestamp and `age_ms` (FR-61) · S / T · S8
- **Setup:** Hub with the AP off, or SC-05 sending a message with a known `age_ms`.
- **Steps:** 1. Produce an event while the uplink is down, restore it exactly 120 s later. 2. Read the event time on the dashboard.
- **Tools:** EQ-08, SC-05.
- **Pass:** A buffered event delayed by a known 120 s shows an event time within ±1 s of the real event.

### TC-62 Event history (FR-62) · S / T · S8
- **Setup:** Dashboard, SC-05.
- **Steps:** 1. Inject 1000 events including a compliance-restored event. 2. Check order (newest first), timestamps and the compliance-restored entry.
- **Tools:** SC-05, browser.
- **Pass:** 1000 injected events are all listed in order with timestamps. A compliance-restored event appears with its timestamp.

### TC-63 Dashboard alerts (FR-63) · S / T · S8
- **Setup:** Dashboard, SC-05.
- **Steps:** 1. Raise NOT_WORN, OFFLINE, `COUNTER_ANOMALY` one at a time and check alert and clear. 2. Stop `HUB_STATUS` and time the alert.
- **Tools:** SC-05, browser.
- **Pass:** Each condition injected raises its alert within 5 s (hub silence: within 185 s) and clears when resolved.

### 4.6 Non-functional (NFR-01 to NFR-13)

### TC-70 Alert latency (NFR-01) · S / T · S9
- **Setup:** Full vest rig (N1 to N5, H1), motor on the hub. Logic analyzer on the contact input and the motor pin.
- **Steps:** 1. Run 30 remove trials for each node type (boot, glove, helmet): 90 trials. 2. Measure the physical remove to the motor ON edge.
- **Tools:** EQ-01, EQ-10, SC-07.
- **Pass:** 30 remove trials per node type: 100% start within 28 s.

### TC-71 Reception reliability (NFR-02) · S / T · S9 (smoke run in S4 and S7)
- **Setup:** Each node at ≤ 2 m line of sight from the hub, no deliberate interference (AS-07). Wi-Fi connected.
- **Steps:** 1. Produce 100 events per node (500 total). 2. For each event check that at least one copy was accepted (`accepted` or the gap analysis).
- **Tools:** EQ-10, SC-12, `dump`, EQ-02.
- **Pass:** ≥ 99% of 100 events per node, at ≤ 2 m line of sight (AS-07).

### TC-72 Hub processing time (NFR-03) · B / T · S3, re-run S7
- **Setup:** H1 with timing instrumentation (callback entry to dispatch). Injector or `inject` hook for the burst.
- **Steps:** 1. Inject 1000 packets and log the maximum time. 2. Inject a burst of 25 copies (5 nodes × 5) at full speed. 3. Measure the NVS write time of the persist task and its effect on step 2 (carry-forward from S0). 4. S7: repeat with Wi-Fi active.
- **Tools:** `inject`, `time`, SC-04.
- **Pass:** Timestamp instrumentation: max ≤ 10 ms per packet over 1000 packets. Burst test: 25 of 25 processed.

### TC-73 Security (NFR-04) · S/R / T,I · S6, re-run S9
- **Setup:** H1, injector, SC-03 forged and replay sets.
- **Steps:** 1. Inject 1000 random forged packets with a valid format, vest and node. 2. Inject 100 replays of valid captured packets. 3. Run TC-42.
- **Tools:** EQ-06, SC-03, SC-04, SC-10, `dump`.
- **Pass:** 1000 random forged packets and 100 replays: 0 accepted. Secret scan clean (FR-42).

### TC-74 Motor safety (NFR-05) · B / T · S5, re-run S9
- **Setup:** H1 with the motor driver or a dummy load. Logic analyzer on the motor pin, capturing across resets (trigger on power-up).
- **Steps:** 1. Run 1 h with an active alert and injected resets (EQ-05), `stall 3` and `fault wdt`. 2. Measure the maximum continuous ON time. 3. Check the pin is low at every boot, from reset release to the start of the motor FSM. 4. `fault guard` (the pattern task hangs while ON): check the ON time is at most 400 ms, `HUB_FAULT` (`GUARD`) is reported (serial, then gateway in S7) and the pattern resumes at the next ON edge.
- **Tools:** EQ-01, EQ-05, hub hooks, SC-07.
- **Pass:** Over 1 h with injected resets, scan stall and WDT reset: the maximum continuous ON time is ≤ 400 ms and the output is low at boot. A forced guard trip is reported.

### TC-75 Counter durability (NFR-06) · B / T · S4
- **Setup:** Node sending with `tx` in a loop, sniffer logging counters, EQ-05 with SC-08.
- **Steps:** 1. Run 1000 random power cuts (seeded, cut anywhere in the transmit and NVS write window). 2. After each restart capture the first counter. 3. Check the log is strictly increasing.
- **Tools:** EQ-05, SC-08, EQ-02, SC-02.
- **Pass:** 1000 random power cuts over a run: the counter log is strictly increasing.

### TC-76 Uplink latency (NFR-07) · S / T · S7
- **Setup:** H1 with Wi-Fi up. Injector and gateway stub on the same PC clock.
- **Steps:** 1. Inject 50 packets spaced about 2 s apart. 2. For each, compute the gateway arrival time minus the injector TX time (minus the known radio time).
- **Tools:** SC-04, SC-05.
- **Pass:** 50 events: 100% within 5 s.

### TC-77 Stability (NFR-08) · S / T · S9 (4 h preliminary run in S7)
- **Setup:** Full rig running normally with periodic events, SC-11 logging `HUB_STATUS` and `BOOT` flags.
- **Steps:** 1. Run 24 h. 2. Count unplanned resets (hub reset reason, node `BOOT=1` without a planned reset). 3. Read the free heap at the start and the minimum.
- **Tools:** SC-11, SC-05.
- **Pass:** 24 h run: 0 unplanned resets, minimum free heap does not fall > 1% of its start value.

### TC-78 Toolchain (NFR-09) · R / I,T · S1, every sprint close
- **Setup:** Arduino IDE 2.3.10 (or the matching CLI), esp32 core 3.0.7, NimBLE-Arduino 2.5.1.
- **Steps:** 1. Check installed versions. 2. Clean build of node and hub, save the build log. 3. Check the release build has no `PPE_TEST_HOOKS`.
- **Tools:** Arduino IDE or CLI.
- **Pass:** Clean build of node and hub with exactly these versions.

### TC-79 Packet size (NFR-10) · R/B / I,T · S1 (inspection), S4 (on air)
- **Setup:** Inspection of `ppe_proto.h` (a `static_assert` of 21 bytes). Sniffer capture.
- **Steps:** 1. Inspect the size definitions and the unit test. 2. Capture packets from a node and measure the advertising data length.
- **Tools:** EQ-02, SC-13.
- **Pass:** Inspection: 21 bytes, confirmed on air by sniffer.

### TC-80 Battery (informational) (NFR-11) · B / A · S9 (data collected in S4 and S7)
- **Setup:** Node and hub on EQ-04.
- **Steps:** 1. Measure node current in deep sleep, awake and transmitting. 2. Measure the hub average current with Wi-Fi. 3. Estimate the life for 1500 mAh and 2200 mAh.
- **Tools:** EQ-04.
- **Pass:** Current is measured and the estimated life is reported. Pass/fail limits are set in a later revision.

### TC-81 Traceability (NFR-12) · R / I · S1 (script), every sprint close
- **Setup:** SC-09, the SRS, traceability, test plan and state machine files.
- **Steps:** 1. Run SC-09. It checks every requirement ID of the SRS appears in traceability with a test ID, every test ID appears in this plan with setup, steps, tools, pass rule and sprint, and every transition in the state machines cites a valid requirement.
- **Tools:** SC-09.
- **Pass:** 100% of requirements in this document appear in `traceability_v1.2.md` with a test ID.

---

## 5. Test infrastructure

### 5.1 Host tests and hardware tests

| | Host unit tests (level U) | Hardware tests (levels B and S) |
|---|---|---|
| Run on | PC, `make test` in `test/host/` (g++ through MSYS2 or WSL), no hardware | Real boards, real clocks, radio, NVS, motor |
| Code under test | Pure C++ modules in `firmware/core/` with no Arduino includes. Clock, NVS and radio come through small interfaces so the tests can fake them | The full firmware, flashed from a tagged commit |
| Modules | `ppe_proto`, `ppe_crypto` (wrapper), `node_sensor_conv`, `node_logic` (filter, confirm, heartbeat), `counter_block`, `hub_pipeline`, `hub_registry`, `hub_nodefsm`, `hub_compliance`, `hub_motor_logic`, `uplink_queue`, `uplink_json` | |
| Time | Simulated clock. A 360 s heartbeat or a 780 s timeout runs in milliseconds | Real time. Tolerances in the AC apply here |
| When | Every commit and before every merge | At each sprint close for the TCs assigned to that sprint, plus the regression sets in 5.5 |
| Proves | Logic, truth tables, counter and filter rules, schema | Timing, radio, power, NVS durability, motor, integration |

Rules:
1. A TC with level U/B (TC-03, 12, 22, 23, 24, 32; TC-51 is U/S and TC-79 is R/B) passes only when the host part and the hardware part both pass. The result file lists both.
2. A host pass never replaces a B or S test. The host harness uses a portable SHA-256 inside `test/host/` only. The device uses mbedtls (FR-12), so the bench run of TC-12 stays required.
3. Every host test checks its expected values against `gen_vectors` output or the SRS numbers, never against the code under test.
4. Test hooks (section 3) are compiled only with `PPE_TEST_HOOKS`.

### 5.2 Hardware run procedure

1. Check out the tagged commit on a clean tree. Build with the exact toolchain (TC-78). Record the commit SHA of node, hub and tools.
2. Flash, provision with SC-01 (test key), set the parameters the TC names.
3. Create the run folder with SC-06, execute the steps, save the artifacts.
4. Compute the result with the analysis script, not by eye. Write `result.json` and `notes.md`.
5. On a FAIL file a bug (`docs/bug_log_template.md`) and link it in `result.json`.

### 5.3 Test logs: location and format

Location (in the repo, small files only): `test_logs/<sprint>/<TC-ID>/<YYYYMMDD-HHMMSS>_<sha7>/`

| File | Content |
|---|---|
| `result.json` | Verdict and measurements (below) |
| `serial_<device>.log` | Timestamped serial output, one line per entry: `ISO-8601 time, device, message` |
| `analysis.txt` | Script output with the computed values |
| `notes.md` | Tester notes, deviations, setup photos list |
| `capture.*` | Sniffer or logic analyzer captures. Files over 5 MB stay outside git and `result.json` holds their path and SHA-256 |

`result.json`:

```json
{
  "tc": "TC-06",
  "sprint": "S2",
  "date": "2026-11-02T14:05:00+05:30",
  "tester": "name",
  "fw": {"node": "a1b2c3d", "hub": "-", "tools": "e4f5a6b"},
  "toolchain": {"ide": "2.3.10", "esp32_core": "3.0.7", "nimble": "2.5.1"},
  "devices": {"N1": "board serial or label"},
  "parameters": {"GRACE_S": 25, "SETTLE_MS": 0},
  "result": "PASS",
  "measurements": {"event_time_s": [25.02, 24.98]},
  "bugs": [],
  "findings": [],
  "artifacts": ["capture_la.csv"]
}
```

`result` is one of `PASS`, `FAIL`, `BLOCKED` (missing equipment or an open observation stops the run), `N/A` (with a reason, for example gloves and helmet in TC-16 under D-15). `test_logs/summary.csv` has one row per run (TC, sprint, date, SHA, result) and is checked by SC-09 at sprint close.

### 5.4 Bug log

Bugs follow `docs/bug_log_template.md`: one file `docs/bugs/BUG-###.md` per bug and an index in `docs/bugs/BUGLOG.md`. Severity: **P1** safety, security or fail-safe violation, or loss of an alert. **P2** requirement not met with no safety impact. **P3** minor, cosmetic or documentation.

### 5.5 Regression rule

| Tier | When | What runs |
|---|---|---|
| R0 | Every commit and before every merge to `develop` | Full host suite (`make test`), clean build of node and hub (TC-78), SC-10 secret scan, SC-09 traceability check |
| R1 | After any fix, and at each sprint close | R0, the TC that failed, every TC of the same requirement group, and the **smoke set**: TC-03, 06, 07, 10, 12, 22, 23, 24, 26, 29, 50 (only those whose sprint is already reached) |
| R2 | A change to a shared file (`ppe_proto.h`, crypto, counter, pipeline, scheduler timing) | R1 plus TC-11, 12, 22, 23, 24, 73 and every TC of the sprint that owns the file |
| R3 | End of S9 (release) | All 56 TCs on one tagged commit, including the 24 h soak (TC-77) |

Fix rule: a P1 or P2 bug is closed only with a regression test (a new host test, or a named TC step) that fails before the fix and passes after it. Re-running the failed TC on the fixed build is part of closing. A change to an approved requirement is never made as a fix, it goes to the owner as a finding.

### 5.6 Definition of Done

**Task (one feature or fix):**
- [ ] Code builds clean with the pinned toolchain (TC-78) and has no `PPE_TEST_HOOKS` in the release build
- [ ] Every state machine transition touched cites a requirement ID and is covered by a host test
- [ ] Host suite passes (R0)
- [ ] The TCs of the requirement ran on hardware with a `result.json` and PASS (or an approved waiver)
- [ ] No P1 or P2 bug open for the task
- [ ] No real key, credential or token in any commit (SC-10)
- [ ] Docs updated (state machines, test plan, findings) when behavior changed

**Sprint:**
- [ ] All TCs assigned to the sprint are PASS on one tagged commit (re-runs included)
- [ ] R1 regression passed on that commit
- [ ] SC-09 reports 100% traceability, `test_logs/summary.csv` complete
- [ ] Bug log reviewed: 0 open P1, 0 open P2 (or waiver signed by the owner), P3 listed
- [ ] Findings assigned to the sprint resolved or deferred by the owner
- [ ] Owner review and sign-off, then merge `feature/*` to `develop`

**Release (end of S9):**
- [ ] R3 passed on one tagged commit
- [ ] 24 h soak passed (TC-77), NFR-11 data reported (TC-80)
- [ ] Secret scan over all commits clean (TC-42)
- [ ] Owner sign-off, merge `develop` to `main`

---

## 6. Sprint map

The sprint breakdown below is an assumption of this plan. The Wi-Fi plus BLE recheck is placed in S3 and S7, the shared `ppe_proto.h` and key handling in S1, and the NVS write time versus NFR-03 in S3 (TC-72).

| Sprint | Focus | TCs first passed here | Count |
|---|---|---|---|
| S1 | Shared core and test infrastructure (`ppe_proto.h`, crypto wrapper, key handling, host harness, scripts) | TC-12, TC-42, TC-78, TC-79, TC-81 | 5 |
| S2 | Node sensing and confirm logic (conversion, settle, grace, markers) | TC-02, TC-03, TC-04, TC-05, TC-06, TC-16 | 6 |
| S3 | Hub receive path and persistence (scan, filter, registry, NVS, Wi-Fi + BLE recheck 1) | TC-20, TC-21, TC-22, TC-23, TC-24, TC-25, TC-32, TC-72 | 8 |
| S4 | Node radio, counter and sleep (transmit, heartbeat, counter block, deep sleep, wake) | TC-01, TC-07, TC-08, TC-09, TC-10, TC-11, TC-13, TC-14, TC-15, TC-75, TC-17 | 11 |
| S5 | Hub logic (node state, compliance, motor, faults, boot) | TC-26, TC-27, TC-28, TC-29, TC-30, TC-31, TC-74 | 7 |
| S6 | Provisioning and security hardening | TC-40, TC-41, TC-43, TC-44, TC-45, TC-73, TC-46 | 7 |
| S7 | Uplink and gateway (queue, retry, `HUB_STATUS`, Wi-Fi + BLE recheck 2) | TC-50, TC-51, TC-52, TC-53, TC-54, TC-76 | 6 |
| S8 | Dashboard | TC-60, TC-61, TC-62, TC-63 | 4 |
| S9 | Integration, soak and release | TC-70, TC-71, TC-77, TC-80 | 4 |

| TC | Sprint | Re-run | Level / method |
|---|---|---|---|
| TC-01 | S4 | - | B / T |
| TC-02 | S2 | - | B / T |
| TC-03 | S2 | - | U/B / T |
| TC-04 | S2 | - | B / T |
| TC-05 | S2 | - | B / T |
| TC-06 | S2 | S4 | B / T |
| TC-07 | S4 | - | B / T |
| TC-08 | S4 | - | B / T |
| TC-09 | S4 | - | B / T |
| TC-10 | S4 | - | B / T |
| TC-11 | S4 | - | B / T |
| TC-12 | S1 | S4 | U/B / T |
| TC-13 | S4 | - | B / T |
| TC-14 | S4 | - | B / T |
| TC-15 | S4 | - | B / T |
| TC-16 | S2 | S4 | B / T |
| TC-17 | S4 | S5 (hub OFFLINE and alert) | B / T |
| TC-20 | S3 | S7, S9 | S / T |
| TC-21 | S3 | - | B / T |
| TC-22 | S3 | - | U/B / T |
| TC-23 | S3 | - | U/B / T |
| TC-24 | S3 | S7 | U/B / T |
| TC-25 | S3 | - | B / T |
| TC-26 | S5 | - | U / T |
| TC-27 | S5 | - | S / T |
| TC-28 | S5 | S7 | U / T |
| TC-29 | S5 | S7 (via TC-53) | B / T |
| TC-30 | S5 | S7 | B / T |
| TC-31 | S5 | S7 | S / T |
| TC-32 | S3 | S7 | U/B / T |
| TC-40 | S6 | - | B / T |
| TC-41 | S6 | - | B / T |
| TC-42 | S1 | every sprint close, S9 | R / I,T |
| TC-43 | S6 | S7 | S / T |
| TC-44 | S6 | - | B / T |
| TC-45 | S6 | - | S / T,I |
| TC-46 | S6 | - | S / T |
| TC-50 | S7 | - | S / T |
| TC-51 | S7 | - | U/S / T |
| TC-52 | S7 | - | S / T |
| TC-53 | S7 | - | S / T |
| TC-54 | S7 | - | S / T |
| TC-60 | S8 | - | S / T |
| TC-61 | S8 | - | S / T |
| TC-62 | S8 | - | S / T |
| TC-63 | S8 | - | S / T |
| TC-70 | S9 | - | S / T |
| TC-71 | S9 | S4, S7 smoke | S / T |
| TC-72 | S3 | S7 | B / T |
| TC-73 | S6 | S9 | S/R / T,I |
| TC-74 | S5 | S9 | B / T |
| TC-75 | S4 | - | B / T |
| TC-76 | S7 | - | S / T |
| TC-77 | S9 | S7 (4 h) | S / T |
| TC-78 | S1 | every sprint close | R / I,T |
| TC-79 | S1 | S4 | R/B / I,T |
| TC-80 | S9 | data in S4, S7 | B / A |
| TC-81 | S1 | every sprint close | R / I |

## 7. Equipment needed by sprint

| Sprint | New equipment and scripts needed |
|---|---|
| S1 | EQ-12, SC-01, SC-02, SC-03, SC-06, SC-09, SC-10, SC-13, SC-14 |
| S2 | EQ-01, EQ-10, SC-07, SC-12, node hooks `adc`, `marker`, `show` |
| S3 | EQ-06, EQ-07, SC-04, minimal SC-05, hub hooks `inject`, `stall`, `dump`, `time` |
| S4 | EQ-02, EQ-03, EQ-04, EQ-05, SC-08, node hooks `tx`, `setcounter`, `nvslog`, `skiptx`, `fault`, `awake`, `adc ok` |
| S5 | EQ-13, hub hook `fault` |
| S6 | Port scanner, BLE scanner |
| S7 | EQ-08, EQ-09, full SC-05, SC-11 |
| S8 | Dashboard on EQ-09 |
| S9 | Full vest rig, all of the above |

## 8. Open items

1. Confirm or replace the sprint breakdown in section 6. It is still an assumption of this plan.
2. Confirm the equipment list in section 2.1 (sniffer and current profiler models are proposals) and the repository layout (`test/host`, `test_logs`, `tools`, `docs/bugs`).
3. Observations V12-01 to V12-09 on SRS 1.2 (`docs/s0_findings_v1.md` section 4) are not decided. The steps that depend on them say which interpretation they use: TC-16 (V12-01), TC-46 (V12-02), TC-30 (V12-03, V12-05), TC-27 and TC-31 (V12-04), TC-17 (V12-06), TC-11 (V12-07), TC-52 (V12-08), TC-21 (V12-09).
