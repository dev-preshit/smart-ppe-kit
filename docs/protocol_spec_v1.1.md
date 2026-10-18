# Smart PPE Compliance: Packet Protocol Specification

| Item | Value |
|---|---|
| Document | `docs/protocol_spec_v1.1.md` (document revision 1.1) |
| Protocol version | **1** (wire value `01b`, unchanged by this revision) |
| Companion | `docs/SRS_v1.2.md`, `docs/traceability_v1.2.md` |
| Status | APPROVED (S0 closed) (resolves S0 findings F-01 to F-20) |
| Applies to | ESP32-C3 nodes (transmit), ESP32-S3 hub (receive), gateway uplink |

Key words MUST, MUST NOT, SHOULD follow RFC 2119. Values marked **[P]** are parameters. They live in the shared `ppe_proto.h` (S1) and can be changed at provisioning within the stated bounds.

### Revision history

| Revision | Change |
|---|---|
| 1 | S0 draft, approved and pushed. Review edits: hub persists `last_counter` after dispatch (6.2), HTTP uplink with a token and gateway timestamps (section 10), AS-09. |
| 1.1 | Boot definition and deep-sleep wake rule (1, 6.1.4, 8.1.6), heartbeat during grace and grace expiry during a sequence (4, 7.5, 8.1), pair monitoring and fault clearing (8.1), boot grace restart (8.1.5), wake loop guard (8.1.7), node faults (6.1.5), `drop_state` and counter scope (2, 9.3), fixed registry slots (8.2), `KEY_FAULT` pattern and guard trip (8.3), uplink detail schema, `hub_seq` rules, buffer coalescing and retry values (section 10). The wire format is unchanged. |

---

## 1. Conventions

- Multi-byte integers are **little-endian**. Bit 7 is the most significant bit of a byte.
- "Logical packet" is one heartbeat or one event. It is transmitted as 5 identical **copies** (section 7).
- "Confirmed state" is the last wear state the node transmitted (or `NONE` after a boot).
- A **boot** is any node start whose reset reason is not deep-sleep wake, or a deep-sleep wake whose RTC context fails its magic and CRC check. A deep-sleep wake restores `confirmed`, `counter`, `ceiling`, the heartbeat deadline and `sensor_fault` from RTC memory and is **not** a boot. The RTC context is trusted only when the reset reason is deep-sleep wake. Power-on, brown-out, watchdog and any other reset use the cold-boot path (`BOOT=1`, counter block advance).

### 1.1 Constants

| Name | Default | Bounds | Meaning |
|---|---|---|---|
| `PROTO_VER` | 1 | fixed | Protocol version in `flags` |
| `HEARTBEAT_S` **[P]** | 360 | 60 to 3600 | Heartbeat period |
| `GRACE_S` **[P]** | 25 | 1 to 120 | Time a changed state must persist before it is sent |
| `SETTLE_MS` **[P]** | 0 | 0 to 1000 | Sensor debounce before a change is accepted as a candidate |
| `RETX_COUNT` | 5 | fixed | Copies per logical packet (1 original + 4 repeats) |
| `RETX_PERIOD_MS` | 500 | fixed | Start-to-start spacing of copies (tolerance ±50 ms) |
| `ADV_BURST_MS` **[P]** | 100 | 50 to 300 | Advertising time of one copy (see AS-05) |
| `ADV_INTERVAL_MS` **[P]** | 20 | 20 to 100 | Advertising interval inside a burst (see AS-05) |
| `COUNTER_BLOCK` | 64 | fixed | NVS counter reservation block |
| `OFFLINE_MISSED` | 2 | fixed | Missed heartbeats before OFFLINE |
| `OFFLINE_MARGIN_S` **[P]** | 60 | 0 to 300 | Margin added to the missed-heartbeat time (tuned at test) |
| `OFFLINE_TIMEOUT_S` | 780 | derived | `OFFLINE_MISSED × HEARTBEAT_S + OFFLINE_MARGIN_S` |
| `BATT_LOW_PCT` | 20 | fixed | Low-battery flag threshold |
| `FSR_WORN_MIN` | 1537 | fixed | FSR count at or above which a boot becomes WORN |
| `FSR_NOT_WORN_MAX` | 88 | fixed | FSR count at or below which a boot becomes NOT_WORN (i.e. below 89) |

---

## 2. Radio layer

- **Mode:** BLE 4.x legacy advertising, PDU type `ADV_NONCONN_IND` (non-connectable, non-scannable). No connections, no scan responses. Advertising address type is not used for identity. Identity is `vest_id` + `node_id` inside the authenticated payload.
- **Channels:** all three advertising channels (37, 38, 39).
- **AD structure:** a single Manufacturer Specific Data AD element and nothing else. No Flags AD, no name.

| Offset | Size | Value | Meaning |
|---|---|---|---|
| 0 | 1 | `0x14` | AD length (20) |
| 1 | 1 | `0xFF` | AD type: Manufacturer Specific Data |
| 2 | 2 | `0xFF 0xFF` | Company ID `0xFFFF` (reserved for test/internal use) |
| 4 | 17 | payload | Packet payload (section 3) |

Total advertising data is **21 bytes** (limit 31).

**Hub receive filter** (in this order, cheapest first). A packet failing any step is dropped and counted (section 9.3).

1. Advertising PDU is `ADV_NONCONN_IND` and the AD data is exactly 21 bytes with the header above.
2. Payload version field equals `PROTO_VER`.
3. `vest_id` equals the hub's `vest_id`.
4. `node_id` is registered in the hub registry.
5. HMAC verifies (section 5).
6. `state` is `0x00` or `0x01`. Otherwise drop (`drop_state`).
7. Counter rule accepts (section 6).

---

## 3. Packet payload (17 bytes)

| Offset | Size | Field | Description |
|---|---|---|---|
| 0 | 1 | `flags` | Version, type, operational flags (section 3.1) |
| 1 | 1 | `vest_id` | Vest/group identifier, `0x01` to `0xFE` (`0x00`, `0xFF` invalid) |
| 2 | 1 | `node_id` | Node identity and role (section 3.2) |
| 3 | 1 | `state` | `0x00` NOT_WORN, `0x01` WORN. Any other value: drop |
| 4 | 1 | `battery` | Battery percent `0` to `100`. `0xFF` = not available |
| 5 | 4 | `counter` | uint32 little-endian, replay counter (section 6) |
| 9 | 8 | `hmac` | First 8 bytes of HMAC-SHA256 (section 5) |

### 3.1 `flags`

| Bits | Name | Values |
|---|---|---|
| 7 to 6 | `VER` | `01b` = protocol v1. `00b`, `10b`, `11b`: drop |
| 5 | `TYPE` | `0` = HEARTBEAT, `1` = EVENT |
| 4 | `BOOT` | `1` = first logical packet since a node boot (power-on or reset, not a deep-sleep wake) |
| 3 | `LOW_BATT` | `1` if `battery` ≤ `BATT_LOW_PCT` |
| 2 | `SENSOR_FAULT` | `1` if the sensor reading was invalid (FSR nodes only, the node then reports NOT_WORN) |
| 1 to 0 | reserved | Sender writes `0`. Receiver ignores. Covered by the HMAC |

### 3.2 `node_id` (identity and role)

| Value | Role | Sensor |
|---|---|---|
| `0x01` | LEFT_BOOT | FSR |
| `0x02` | RIGHT_BOOT | FSR |
| `0x03` | LEFT_GLOVE | metal strip + transistor |
| `0x04` | RIGHT_GLOVE | metal strip + transistor |
| `0x05` | HELMET | crown push switch AND chin-strap contact |

Left and right nodes run the same binary. `node_id` is written at provisioning. Other values are invalid.

---

## 4. Packet types

| | HEARTBEAT (`TYPE=0`) | EVENT (`TYPE=1`) |
|---|---|---|
| Sent when | `HEARTBEAT_S` after the start of the previous transmission of any type | Confirmed state changes, or first packet after boot (`BOOT=1`) |
| `state` | Current confirmed state | New confirmed state |
| Purpose | Liveness, battery, state refresh | Immediate wear/remove notification |

- **Hub handling is identical:** every accepted packet updates `state`, `battery`, `flags` and `last_seen` for that node. Heartbeats therefore also repair a missed event.
- An event is only generated after the grace period (section 8.1). An event is never sent for a state change that reverted inside the grace period.
- The heartbeat timer restarts at the start of every transmission sequence (heartbeat or event), because any valid packet proves liveness.
- If a heartbeat is due while a sequence is in progress, it starts after the sequence ends. Sequences never interleave, so counters are never reordered.
- A heartbeat due while the node is in grace (awake, no sequence running) is sent at its due time with the confirmed state, and grace continues.

---

## 5. Authentication

- **Key:** one 256-bit key per vest, shared by the hub and its 5 nodes, loaded at provisioning from outside the repository. The real key is never committed (SRS FR-42).
- **Algorithm:** HMAC-SHA256 (mbedtls).
- **Input:** the 9 bytes at payload offsets 0 to 8, in wire order: `flags | vest_id | node_id | state | battery | counter[0..3]`.
- **Output:** the first 8 bytes of the 32-byte digest are placed at offsets 9 to 16 (no other truncation or encoding).
- **Verification:** the hub recomputes the HMAC over bytes 0 to 8 and compares all 8 bytes with a **constant-time** comparison. On mismatch the packet is dropped and `hmac_fail` is incremented. The counter is **not** examined or updated for a packet that fails the HMAC.
- Because the HMAC covers `flags`, `battery` and `state`, none of these can be altered in flight without detection. The 64-bit tag gives a forgery probability of 2⁻⁶⁴ per attempt, and replay is blocked by the counter.

---

## 6. Counter and replay rules

### 6.1 Node

1. The counter is a per-node uint32. The first logical packet after provisioning uses the first value above the stored baseline.
2. The counter increases by **exactly 1 per logical packet**. All 5 copies of that packet carry the same counter and the same bytes.
3. **NVS persistence uses a reserved block.** NVS stores `ceiling`. RAM holds `counter`. When `counter` reaches `ceiling`, the node writes `ceiling + COUNTER_BLOCK` to NVS **before** using any value past the old ceiling.
4. On a boot (section 1: power-on, reset, brown-out, watchdog; not a deep-sleep wake), `counter = ceiling` read from NVS, then `ceiling` is advanced by `COUNTER_BLOCK`. The counter therefore never repeats and never decreases across resets, at the cost of skipping up to `COUNTER_BLOCK` values (the hub treats this as a normal gap). On a deep-sleep wake the counter and ceiling are restored from RTC memory and no NVS write occurs.
5. **Node faults.** If `counter` would exceed `0xFFFFFFF0`, the counter-block NVS write fails, or the BLE stack fails to start after one retry, the node enters `NODE_FAULT`: it stops transmitting and reports the cause on serial. It never transmits without a persisted counter block, so a counter cannot repeat. A counter-limit fault requires re-provisioning. The other faults clear on a reset. The hub sees the node as OFFLINE.

### 6.2 Hub

For a packet that passes the HMAC for node `n`, with `L` = the hub's stored `last_counter[n]` and `c` = packet counter:

| Condition | Result |
|---|---|
| `c > L` | **Accept.** `last_counter[n] = c` is updated in RAM and the packet is dispatched. `last_counter[n]` is then written to NVS immediately after, from a non-blocking task. `missed = c - L - 1` is added to `gap_count[n]`. A gap is normal (lost packets, node reset) and is not an error |
| `c == L` | **Duplicate** (another copy of an accepted packet). Dropped silently. Counted in `dup_count`. Does not change `last_seen` |
| `c < L` | **Replay or counter anomaly.** Dropped. `COUNTER_ANOMALY` is reported to the dashboard (rate-limited to once per node per 60 s). The node state is not changed |

The hub stores `last_counter` per node in NVS and persists it on every accepted packet, so replays are still rejected after a hub reset. A hub reset inside the short window between dispatch and the NVS write can let a later copy of the same packet be accepted again (same state, harmless). Initial `last_counter` after provisioning is `0`.

---

## 7. Retransmission rules

1. Every logical packet is transmitted as `RETX_COUNT` (5) copies. This applies to events **and** heartbeats.
2. Copy `k` (k = 0 to 4) starts at `t0 + k × RETX_PERIOD_MS` (500 ms start-to-start, tolerance ±50 ms). Each copy is a burst of `ADV_BURST_MS`, and the radio is idle between bursts.
3. All copies are byte-identical (same counter, same HMAC). The node does not recompute anything between copies.
4. The sequence is **bounded**. After the 5th copy the node does not retransmit again, and there is no acknowledgment. The sequence length from first copy start to last copy end is at most `4 × 500 + ADV_BURST_MS` = 2.1 s.
5. A state change during a retransmission sequence does not interrupt it. The grace timer runs from the candidate change. If it expires while a sequence is in progress, the EVENT starts when that sequence ends (section 8.1).
6. The hub accepts the first copy it receives (counter rule 6.2) and silently drops the rest.

---

## 8. Timing and offline logic

### 8.1 Node: settle, grace, confirm

1. **Settle:** a raw sensor change becomes the *candidate state* only after it has held for `SETTLE_MS` (0 = immediate). The monitored value is the pair (candidate `state`, `sensor_fault`). A change in either value starts grace and produces an EVENT after `GRACE_S`, including the fault clearing.
2. **Grace:** if the candidate state differs from the confirmed state, the node starts a `GRACE_S` timer. The timer runs only while the candidate **continuously** differs from the confirmed state.
3. If the candidate returns to the confirmed state before the timer expires, the timer is cancelled, **nothing is transmitted**, and the node returns to sleep. A later change starts a **new** full `GRACE_S` timer.
4. When the timer expires, the node sets the confirmed state to the candidate and transmits an EVENT sequence (`BOOT=0`).
5. **Boot:** after a boot (section 1) the confirmed state is `NONE`. The node reads its sensor and decides WORN or NOT_WORN (for FSR dead-band readings with no history: NOT_WORN, fail-safe). `SETTLE_MS` is not applied to this first reading. It waits `GRACE_S`, then transmits an EVENT with `BOOT=1`. If the candidate changes during the wait, the timer restarts, so the state must be stable for `GRACE_S`. No UNKNOWN value is ever transmitted.
6. After an event the node sets its deep-sleep wake level to the **opposite** of the confirmed state (level-triggered GPIO wake flips after each event) and sleeps. A timer wake is also armed for the next heartbeat. On a deep-sleep wake the node restores its state from RTC memory (section 1) and continues without a boot.
7. **Wake loop guard:** after 2 consecutive GPIO wakes with no candidate change (for example an FSR count in the dead band, or a helmet with only one contact closed), the node arms a timer-poll wake (period ≤ 1 s, set in S2) until the reading leaves the condition, then re-arms the GPIO wake.

**FSR hysteresis (boots):** NOT_WORN to WORN only when the filtered count is ≥ 1537. WORN to NOT_WORN only when the filtered count is ≤ 88. Counts in between keep the current candidate. **Helmet:** WORN only when crown switch **AND** strap contact are both closed; either open = NOT_WORN.

### 8.2 Hub: node state and offline logic

Each registry entry has one of four states:

| State | Meaning | Compliant? | Motor alert? |
|---|---|---|---|
| `UNKNOWN` | No valid packet since hub boot (internal only, never on air) | No | **No** |
| `WORN` | Last accepted packet had `state=1` | Yes | No |
| `NOT_WORN` | Last accepted packet had `state=0` | No | **Yes** |
| `OFFLINE` | No accepted packet for `OFFLINE_TIMEOUT_S` | No | **Yes** |

Transitions:

- Registry: the slots for `node_id` `0x01` to `0x05` always exist (compile-time list). If the stored registry cannot be read, the slots still exist with no accepted state, so the nodes go `OFFLINE` after `OFFLINE_TIMEOUT_S` and the alert runs.
- Hub boot: every node `UNKNOWN`, `last_seen = boot time`. `UNKNOWN` occurs only here.
- Accepted packet: state becomes `WORN` or `NOT_WORN` from the packet's `state` (from any previous state, including `OFFLINE` and `UNKNOWN`). `last_seen = now`.
- Timeout: if `now - last_seen[n] > OFFLINE_TIMEOUT_S` (default 780 s = 2 × 360 + 60 margin), state becomes `OFFLINE`. Timeout is evaluated at least once per second. A node that never sends after hub boot goes `OFFLINE` after the same timeout from boot.
- A node that resets and reconnects is shown to the dashboard as its previous state until its first post-boot packet (about `GRACE_S` after reset). Packets with `BOOT=1` are reported to the dashboard as `NODE_REBOOT`.
- Vest compliance: **COMPLIANT** iff all 5 nodes are `WORN`. Otherwise NON_COMPLIANT.

### 8.3 Motor alert (hub)

- Alert is active whenever at least one node is `NOT_WORN` or `OFFLINE`. It starts immediately after the packet is accepted (or the timeout is detected).
- Pattern: **ON 300 ms, OFF 700 ms** (may be revised during development), repeating (1 s period). The output is never continuously on. A hardware/timer guard forces the output off if it is on for more than 400 ms. A guard trip forces the output low, is logged and is reported as `HUB_FAULT` (`GUARD`). The pattern resumes at the next ON edge.
- The alert stops as soon as no node is `NOT_WORN` or `OFFLINE` (no more than one pattern period later). `UNKNOWN` never causes an alert.
- Alerts are local. They do not depend on Wi-Fi or the dashboard.
- **Key fault:** if the hub has no key at boot it raises `HUB_FAULT` (`NO_KEY`) immediately and runs the `KEY_FAULT` pattern instead of the alert pattern: 3 × (ON 100 ms, OFF 100 ms), then OFF 1700 ms, repeating (period 2.3 s). The pattern has priority over the alert pattern, no packet is accepted, and it ends only when a key is provisioned and the hub restarts. The pattern may be revised during development.

---

## 9. Resync flow and diagnostics

### 9.1 Normal reset (no action needed)

A node reset advances the counter past `ceiling` (6.1.4), so the next packet has `c > L` and is accepted. The hub records the `BOOT=1` flag and a counter gap.

### 9.2 Counter loss (flash erased, board replaced, hub registry wiped)

1. The hub drops packets with `c < L` and reports `COUNTER_ANOMALY` for node `n` to the dashboard.
2. The operator **re-provisions** over serial: the hub command `reset-counter <node_id|all>` sets `last_counter` to `0` and logs `COUNTER_RESET` (reported to the dashboard). If the node was replaced or its NVS erased, it is also re-provisioned (ID, vest, key, counter baseline `0`).
3. The next valid packet (`c > 0`) is accepted. There is **no over-the-air resync window**. Provisioning is serial-only.

### 9.3 Hub diagnostic counters (in RAM, reported in `HUB_STATUS`)

- **Global** (the node is not yet trusted when they are counted): `rx_total`, `drop_len`, `drop_ver`, `drop_vest`, `drop_node`, `drop_state` (valid HMAC, invalid `state`, checked after the HMAC and before the counter rule), `scan_quiet_60s` (times 60 s passed with no advertisement of any kind; logged only, never causes a restart).
- **Per node:** `hmac_fail`, `dup_count`, `replay_count`, `gap_count`, `accepted`.

---

## 10. Hub to gateway message (uplink)

Transport is HTTP POST of JSON to the gateway on the local network, with a shared token in a request header (see AS-09), one message per object, order preserved. Common fields:

| Field | Type | Notes |
|---|---|---|
| `msg_type` | string | `PACKET`, `NODE_OFFLINE`, `NODE_REBOOT`, `COMPLIANCE`, `COUNTER_ANOMALY`, `COUNTER_RESET`, `HUB_BOOT`, `HUB_FAULT`, `HUB_STATUS` |
| `vest_id` | uint | |
| `node_id` | uint | present for node messages |
| `state` | string | `WORN`, `NOT_WORN`, `OFFLINE`, `UNKNOWN` for node messages. `COMPLIANT` or `NON_COMPLIANT` for `COMPLIANCE` |
| `pkt_type` | string | `HEARTBEAT` or `EVENT` for `PACKET` |
| `battery` | uint | percent, 255 = n/a |
| `flags` | uint | raw `flags` byte |
| `counter` | uint | node counter |
| `hub_seq` | uint | hub message sequence, increments per generated message (detects loss). Starts at 0 at each hub boot |
| `age_ms` | uint | milliseconds since the hub generated the message at send time (0 if sent immediately) |
| `detail` | object | per `msg_type` content, section 10.1 (additive, absent for `PACKET`) |

The hub keeps no clock for event timestamps. The gateway computer stamps each message on arrival and sets event time = arrival time − `age_ms`.

### 10.1 `detail` object per `msg_type`

| `msg_type` | `detail` fields |
|---|---|
| `PACKET` | none (the common fields carry the packet) |
| `NODE_OFFLINE` | `last_seen_age_s` |
| `NODE_REBOOT` | `missed` (counter gap at the `BOOT=1` packet) |
| `COMPLIANCE` | `nodes_not_ok` (list of `node_id` that are not WORN) |
| `COUNTER_ANOMALY` | `received_counter`, `last_counter` |
| `COUNTER_RESET` | `scope` (`node` or `all`), `node_id` when `scope` is `node` |
| `HUB_BOOT` | `reset_reason`, `fw` |
| `HUB_FAULT` | `code` (`SCAN_STALL`, `WDT_RESET`, `NVS_READ`, `NVS_WRITE`, `NO_KEY`, `GUARD`, `REGISTRY`) |
| `HUB_STATUS` | `uptime_s`, `nodes` (per node: `node_id`, `state`, `battery`, `flags`, `last_seen_age_s`), `diag` (section 9.3), `buffer_fill`, `dropped`, `superseded` |

The full schema is `tools/schema/uplink.schema.json` (S1). `COMPLIANCE` is sent on every change of the vest state. The initial NON_COMPLIANT at hub boot is not a transition, so the dashboard derives it from `HUB_BOOT` and `HUB_STATUS`. A `BOOT=1` packet produces `PACKET` then `NODE_REBOOT`.

### 10.2 Sequence numbers and delivery

- `HUB_BOOT` is the first message after a hub boot, with `hub_seq = 0`. `hub_seq` is gap-free within a boot in a loss-free run. It is not persisted.
- The gateway de-duplicates retried POSTs on (boot epoch, `hub_seq`), where the epoch increments on each `HUB_BOOT`. This keeps delivery exactly-once when a response is lost.
- While the uplink is down the hub buffers up to 100 messages and drops the oldest on overflow. At most one `HUB_STATUS` is held: a newer one replaces the older (counted in `superseded`, not in `dropped`). Missing `hub_seq` values therefore equal `dropped` + `superseded`.
- Retry: 1 s, doubling, capped at 30 s, reset on success. HTTP timeout 2 s. Wi-Fi reconnect: 5 s, doubling, capped at 30 s.
- After a hub reboot caused by a fault, the fault code is kept in RTC memory and reported after boot, because the RAM queue is lost.

---

## 11. Versioning

- The version is in `flags` bits 7 to 6. A receiver MUST drop a packet with an unsupported version before computing the HMAC.
- Document revision 1.1 does not change the wire format. `PROTO_VER` stays 1.
- Adding reserved flag bits or new AD data is a minor change. Changing offsets, field sizes, HMAC input or truncation requires a new version number.

---

## Appendix A: Test vectors

**Test key (TEST ONLY, never use in the product):** `00 01 02 03 … 1E 1F` (32 bytes, `0x00` to `0x1F`).

**A.1 EVENT, vest 1, LEFT_GLOVE, WORN, battery 90%, counter 258**

| Item | Bytes |
|---|---|
| HMAC input (9 B) | `60 01 03 01 5A 02 01 00 00` |
| HMAC-SHA256 (full) | `3222111bd50a5b34d531d0ce9aa7dbd39f985e938c82f9c6f10158d5733ca3c3` |
| Truncated tag (8 B) | `32 22 11 1B D5 0A 5B 34` |
| Payload (17 B) | `60 01 03 01 5A 02 01 00 00 32 22 11 1B D5 0A 5B 34` |
| Advertising data (21 B) | `14 FF FF FF 60 01 03 01 5A 02 01 00 00 32 22 11 1B D5 0A 5B 34` |

Flags `0x60` = version 01, type EVENT, no other flags. Counter 258 = `0x00000102`, wire `02 01 00 00`.

**A.2 HEARTBEAT, vest 1, HELMET, NOT_WORN, battery 50%, counter 259**

| Item | Bytes |
|---|---|
| HMAC input (9 B) | `40 01 05 00 32 03 01 00 00` |
| Truncated tag (8 B) | `41 B4 7A 3B 28 82 2B 6A` |
| Payload (17 B) | `40 01 05 00 32 03 01 00 00 41 B4 7A 3B 28 82 2B 6A` |

Vectors were generated with Python `hmac`/`hashlib` using the test key above.

---

## Appendix B: Assumptions affecting this specification

| ID | Assumption |
|---|---|
| AS-05 | `ADV_BURST_MS` = 100 ms and `ADV_INTERVAL_MS` = 20 ms are provisional. They are to be confirmed against the S0 BLE spike (T1 to T8) results. |
| AS-09 | The v1 uplink is HTTP POST with JSON and a shared token header, from the hub to a gateway computer on the local network. HTTPS with SNTP time sync on the hub is a later option. To be rechecked in S3 and S7 with the Wi-Fi plus BLE coexistence check. |
| AS-10 | Company ID `0xFFFF` is acceptable for v1 (internal use only). |
