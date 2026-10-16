# S1 conflict and gap report

Reported separately as instructed. **No source document was changed.** IDs V12-10 to V12-17 are proposals, to be added to
`docs/s0_findings_v1.md` section 4 by the owner if accepted. "Brief" means the S1 chat brief.

## A. Conflicts between the brief and the documents (need your decision)

| # | Conflict | What I did | Decision needed |
|---|---|---|---|
| C-01 | Test plan 5.1 puts pure modules in `firmware/core/`; the brief says `common/` (Arduino library). | Followed the brief: `common/`. | Confirm, and update the test plan wording at S1 close. |
| C-02 | DONE WHEN lists "136 bit flips rejected" (that is TC-23, S3) and "no counter repeat across 1000 power cuts" (TC-75/NFR-06 and TC-11 step 5, S4). Test plan section 6 gives S1 only TC-12, 42, 78, 79, 81. | Built and ran both as host tests. They are logged as early evidence, not as TC-23/TC-75 passes. | Confirm that is the reading you want for "done". |
| C-03 | Agenda 1 asks for mbedtls for host builds; test plan 5.1 rule 2 says the host harness uses a portable SHA-256 in `test/host/` only. | `ppe_crypto.cpp` uses mbedtls on host and device (same production code). A portable SHA-256/HMAC in `test/host/` is used as an independent oracle. Both are exercised. | Confirm. |
| C-04 | FR-42: the real key shall not exist in source or build files. The brief asks for `secrets.example.h` (implies a local `secrets.h`). | `secrets.example.h` is a placeholder; `secrets.h` is gitignored and only for throwaway keys; the real key is for serial provisioning (S6). The scan also searches ignored working-tree files, so a real key in `secrets.h` is a finding. | Confirm. |
| C-05 | Test plan 2.2 and 7 assign SC-01 `provision.py` and SC-06 `logger.py` to S1; the brief's story list omits them, and no document defines the provisioning serial command syntax (V-01 lists fields only). | Not built. `s1_run.py` writes the log files itself for host runs. | Add them to S1, or move them (SC-06 is needed by S2 hardware runs). |
| C-06 | The brief says the node/hub spike copies of `ppe_proto.h` must be gone. The spike sketches were not in the uploads. | Could not delete them. The TC-79 run fails if any second `ppe_proto.h` exists in the tree, so your first run will point at them. | Delete them and include `<ppe_common.h>`. |

## B. Documents: defects found

| ID | Where | Defect | Sev |
|---|---|---|---|
| V12-10 | test plan 5.5, rule R3 | Says "All 56 TCs on one tagged commit". Traceability, sprint map and SC-09 all give 58 test cases. SC-09 does not check prose counts, so it passed. | P3 |

## C. Gaps in the spec (I chose a safe reading; please rule)

| ID | Gap | Reading implemented |
|---|---|---|
| V12-11 | Spec 6.1.5 covers a block-write failure only. A failed NVS *read* of the ceiling at cold boot is not specified. | Same as a write failure: `PPE_CTR_ERR_NVS` (NODE_FAULT), no transmit, because no repeat can be ruled out. |
| V12-12 | N-04 says write NVS before any transmit. When the ceiling is already at the 0xFFFFFFF0 clamp the new value equals the stored one. | The write is skipped (value unchanged, no information lost). The first `next()` then returns `PPE_CTR_ERR_LIMIT` anyway. |
| V12-13 | Spec 10 does not say which common fields each `msg_type` must carry. | Schema (`uplink.schema.json`): every type has `msg_type, vest_id, hub_seq, age_ms`. PACKET additionally needs `node_id, state, pkt_type, battery, flags, counter` and forbids `detail`. NODE_OFFLINE: `node_id`, `state` = OFFLINE. NODE_REBOOT: `node_id`, `state` WORN/NOT_WORN, `detail.missed`. COMPLIANCE: `state`, `detail.nodes_not_ok` (empty iff COMPLIANT). COUNTER_ANOMALY: `node_id`, `detail.received_counter/last_counter`. COUNTER_RESET: `detail.scope` (node needs `detail.node_id`). HUB_BOOT: `hub_seq` = 0. Top level allows no extra fields; `detail` allows extra (spec calls it additive). |
| V12-14 | Spec 9.3 / 10.1 say HUB_STATUS carries `diag` but not its layout. | `diag` = the seven global drop counters plus `nodes[5]` with `hmac_fail, dup_count, replay_count, gap_count, accepted`. `detail.nodes[5]` holds per-node state, battery, flags, `last_seen_age_s`. |
| V12-15 | Spec 2 step 1 says "PDU is ADV_NONCONN_IND" but the NimBLE 2.5.1 event-type encoding is not stated. | The core takes a `bool pdu_is_nonconn_adv`; the S3 BLE glue maps the NimBLE event type. |
| V12-16 | Spec 11 vs ESP-IDF: the host test uses system mbedtls 2.28.8; core 3.0.7 ships mbedtls 3.x. | Only `mbedtls_md_info_from_type` and `mbedtls_md_hmac` are used (same signature in 2.x and 3.x). The device build and the S4 bench run are what prove it. |
| V12-17 | TC-12 is U/B. Test plan 5.2 rule 1 says a U/B case passes only when host and hardware both pass, but section 6 lists TC-12 as an S1 first pass ("S1 host, S4 bench"). | Logged TC-12 as PASS with `measurements.scope` = host part only and the bench part named as owed in S4. |

## D. Carry-forward notes from this sprint

- Counter block advance at every cold boot burns up to 64 values per boot. At 0xFFFFFFF0 that is 67 million boots, no practical issue; noted for NFR-06.
- `PPE_LOG*` has no key redaction by construction: the rule is "never pass key bytes", enforced by review and by the secret scan, not by code.
- Your carry-forward items stay open: Wi-Fi plus BLE recheck (S3, S7), NVS write time vs NFR-03 (S3), RTC context and deep sleep (S2, S4). `PpeNodeCounter::resume()` is the hook for the RTC case.
