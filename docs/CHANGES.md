# CHANGES (frozen documents are not edited; one line per deviation: date | what | why)

2026-10-19 | Hub pipeline and pure logic live in firmware/hub/src/ (no Arduino includes), compiled by test/host; test plan 5.1 names firmware/core/ | no new library link, common/ untouched
2026-10-19 | S3 evidence is one serial log per test plus one summary.csv line, not the test plan 5.3 run folders | lean process
2026-10-19 | TC-20 runs 20 min (120 beacons, at most 1 miss) instead of 1 h | lean process
2026-10-19 | rx_total is counted in the pipeline, not in the BLE callback; extra internal counter queue_full (dump only) | keeps the callback minimal (P-01)
2026-10-19 | TC-22 bad length and bad AD header defects are fed by the hub inject hook, all other defects on air | NimBLE may rewrite malformed advertising data
2026-10-19 | New hub test hook "rxlog on|off" (PPE_TEST_HOOKS only), lists every received advertisement before filtering | lets S4 observe copies and spacing without a sniffer
2026-10-19 | last_counter keys in NVS: namespace "ppehub", keys lc1..lc5; a failed NVS write is counted and not retried in a loop (the RAM counter stays authoritative, the next accepted packet queues a new write) | spec 6.2 gives no key layout or failure rule; matches the fail-safe replay protection
