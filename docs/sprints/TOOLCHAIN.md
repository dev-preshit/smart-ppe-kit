# TOOLCHAIN.md

Single source of truth for the build environment. Update whenever anything changes.

## Locked config (Config A) - ACCEPTED after S0
| Item | Value |
|---|---|
| Arduino IDE | 2.3.10 |
| esp32 core (Espressif) | 3.0.7 |
| NimBLE-Arduino | 2.5.1 |
| Fallback (Config B) | older NimBLE that worked before - not needed, version never recovered |

## Board settings
| Target | Board | Notes |
|---|---|---|
| Node | ESP32C3 Dev Module | USB CDC On Boot: Enabled |
| Hub | ESP32S3 Dev Module | USB CDC On Boot: Enabled |

## S0 results - Config A
| Test | Result | Notes |
|---|---|---|
| T1 node builds | PASS | after API fix, see P3-1 |
| T2 hub builds | PASS | |
| T3 hub sees node ads | PASS | RSSI about -47 to -59 dBm at bench distance, ~27 repeats per 1 s burst |
| T4 HMAC verifies | PASS | 8-byte truncated HMAC-SHA256 |
| T5 corrupted MAC rejected | PASS | every 10th counter corrupted; all rejected, counter not advanced |
| T6 counter survives power cycle | PASS | ctr 50 -> unplug -> ctr 51, no replay |
| T7 deep-sleep loop | PASS | ~170 cycles total, replay=0, gaps = corrupted counters only; real deep sleep confirmed by user |
| T8 5 nodes at once | PASS | all 5 node IDs, real deep sleep, ~4+ min observed at the hub: gaps == bad for every node (no lost good bursts), replay frozen after one event, see P3-4 |

## Decision
- [x] Config A accepted
- [ ] Fall back to Config B

## Known issues
| ID | Pri | Issue | Workaround |
|---|---|---|---|
| P3-1 | P3 | NimBLE-Arduino 2.5.1 has no `setConnectable()`; use `setConnectableMode(BLE_GAP_CONN_MODE_NON)` | fixed in node_test |
| P3-2 | P3 | Arduino IDE serial monitor stalls then flushes lines in a burst; C3 USB port drops during deep sleep | use the hub as the observer for node tests; DEBUG_NO_SLEEP=1 for bench debugging |
| P3-3 | P3 | 1 of ~170 advertising bursts never received by the hub (it was a corrupted-MAC one) | offline rule is 2 missed heartbeats so a single miss is tolerated; recheck with Wi-Fi + BLE scan running together on the S3 hub |
| P3-4 | P3 | A node whose NVS counter restarts (flash erase / re-flash) is rejected by the hub as replay until its counter passes the old value (seen on RIGHT_GLOVE: ~10 bursts rejected, then recovered) | SRS item: define counter-resync / re-pairing or a bounded acceptance window; at 300 s heartbeat a strict rule could mean hours offline |
