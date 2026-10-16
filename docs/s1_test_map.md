# S1 test map

Source of truth: `docs/test_plan_v1.md` section 6 (sprint map). S1 first-pass test cases: **TC-12, TC-42, TC-78, TC-79, TC-81**.

## S1 verdicts (latest run: see `test_logs/summary.csv`)

| TC | Pass rule (SRS acceptance criterion) | What S1 checks | Status after the sandbox run | What closes it |
|---|---|---|---|---|
| TC-12 | Node output matches Appendix A with the test key; independent Python verifier accepts 1000 of 1000 captured packets | Host part (test plan: "S1 host, S4 bench"): A.1 and A.2 reproduced byte for byte, mbedtls wrapper vs independent oracle, SC-02 verifier unit-tested on a 1000-packet synthetic capture | PASS (host part) | Bench part in S4 (node on air + sniffer + SC-02 on a real capture) |
| TC-79 | 21 bytes by inspection, confirmed on air | `static_assert`s in `ppe_proto.h`, unit test, single-copy check of `ppe_proto.h` in the tree | PASS (inspection part) | On-air length by sniffer in S4. The single-copy check fails on your repo until the two spike copies are deleted |
| TC-81 | 100% of requirements appear in traceability with a test ID | SC-09 on the five documents + `summary.csv` completeness | PASS (59/59 requirements, 58 test cases, 203 transitions) | nothing, re-run at every sprint close |
| TC-42 | 0 occurrences of the real key in all commits; builds with no key in source | secret scan (generic pattern mode only in the sandbox), .gitignore and tracked-file hygiene, host build with no key | **BLOCKED** | `python tools/s1_run.py --key-file <real key file> --build --ide 2.3.10` on your machine |
| TC-78 | Clean build of node and hub with exactly the pinned versions | `tools/build_check.py` (fake-CLI unit tests only here) | **BLOCKED** | fill `tools/build_targets.txt`, then the same command as above |

## Every host test and the TC it supports

Run `make -C test/host list` for the live list. S1-owned rows first.

| Test (file) | TC | Owner sprint | Evidence type |
|---|---|---|---|
| size_constants_21_bytes (test_proto) | TC-79 | S1 | S1 pass (inspection part) |
| a1_vector_adv_matches_spec, a2_vector_payload_matches_spec, crypto_wrapper_matches_independent_hmac, encode_decode_roundtrip, flag_helpers, ct_equal_detects_every_byte (test_proto) | TC-12 | S1 | S1 pass (host part) |
| log_macro_levels_and_sink (test_log) | TC-12 | S1 | support code, no dedicated TC |
| filter_defect_cases_match_generated_expectations, filter_null_adv_is_drop_len (test_filter) | TC-22 | S3 | early host evidence |
| a1_accepted_and_decoded, all_136_bit_flips_rejected (test_filter) | TC-23 | S3 | early host evidence (your "136 bit flips" item) |
| counter_rule_table, five_copies_and_100_replays (test_filter) | TC-24 | S3 | early host evidence |
| drop_counters_equal_injected (test_filter) | TC-32 | S3 | early host evidence (filter results only; the counters live in S3 code) |
| forged_1000_all_rejected_by_hmac (test_filter) | TC-73 | S6 | early host evidence |
| first_packet_after_provisioning_is_baseline_plus_one, increments_by_one_and_one_write_per_64, million_packets_fake_nvs, hundred_forced_resets_never_repeat_or_decrease, deep_sleep_resume_makes_no_nvs_write (test_counter) | TC-11 | S4 | early host evidence (step 5 of TC-11 and the logic behind steps 1 to 4) |
| limit_clamp_and_counter_limit_fault, nvs_write_failure_means_no_counter_issued, cold_boot_nvs_failures_block_transmit (test_counter) | TC-17 | S4 | early host evidence for N-09, N-30 |
| thousand_simulated_power_cuts_counter_strictly_increasing (test_counter) | TC-75 | S4 | early host evidence (your "1000 power cuts" item; includes cuts inside the NVS write, with and without the write reaching flash) |

The "early host evidence" rows are not passes of those test cases. Their bench or system parts, and the code that is not in S1 (hub registry, node scheduler), still have to run in their own sprint.

## Tool tests (`python -m unittest discover -s tools/tests`)

| File | Covers |
|---|---|
| test_schema.py | SC-14 `tools/schema/uplink.schema.json`: a valid example of every `msg_type`, 30 invalid examples (TC-51 host part, S7) |
| test_verify_packets.py | SC-02 (TC-12) |
| test_secret_scan.py | SC-10 (TC-42): key planted as hex, upper hex, spaced, colon, wrapped C array, base64, decimal list, raw binary, commit message, other branch, deleted-from-history, ignored working-tree file |
| test_check_traceability.py | SC-09 (TC-81): 8 kinds of document damage are detected |
| test_build_check.py | `build_check.py` against a fake arduino-cli |

`tools/mutation_check.py` applies 10 deliberate bugs (wrong HMAC bytes, short compare, un-persisted block, no boot advance, no limit, filter order, duplicate accepted, byte order, verify-always-true) and requires the suite to fail for each. All 10 are killed.
