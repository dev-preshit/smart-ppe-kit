# Vest Hub (ESP32-S3), sprint S3: receive path and persistence

Scope of S3: BLE scan, receive pipeline (spec section 2), 5-slot registry, diagnostic counters, `last_counter` in NVS,
scan supervisor (FR-21), bench test hooks. Out of scope here: compliance, motor, faults (S5), provisioning (S6),
uplink queue and retry (S7). Frozen documents are not edited; deviations are one line each in `docs/CHANGES.md`.

## Files

| Path | Role |
|---|---|
| `hub.ino` | `setup()`/`loop()`, start-up refusal, serial hook dispatch (hooks only with `PPE_TEST_HOOKS`) |
| `hub_ble.h/.cpp` | NimBLE glue: minimal callback, rx queue and task, supervisor task, hook entry points |
| `hub_nvs.h/.cpp` | Preferences store (`ppehub`, keys `lc1`..`lc5`), low-priority writer task, NVS write-time stats |
| `hub_wifi_recheck.*` | Bench only (`PPE_S3_WIFI_RECHECK`): Wi-Fi + one HTTP POST every 30 s |
| `src/hub_pipeline.*` | Pure C++: steps 1-7, registry, counters, anomaly rate limit, `HubStore` interface, start-up check |
| `src/hub_persist.*` | Pure C++: coalescing "newest per node" write queue logic |
| `src/hub_scan_supervisor.*` | Pure C++: FR-21 decisions with an injected clock, `scan_quiet_60s` |
| `src/hub_hooks.*`, `src/hub_dump.*` | Pure C++, compiled only with `PPE_TEST_HOOKS`: serial command parser, `dump` format |
| `build_opt.h.bench`, `build_opt.h.bench_wifi` | Copy to `build_opt.h` (git-ignored) for a bench build |
| `wifi_secrets.example.h` | Copy to `wifi_secrets.h` (git-ignored) and fill in; never commit the real one |

Host tests for everything under `src/` live in `test/host` (`make -C test/host test`, 66 tests, includes a release check
that the hook code leaves no strings in a release object). `tools/mutation_check.py` proves the suite can fail.

## Data path

```
NimBLE host task --onResult--> copy (len, nonconn flag, micros) --queue(64)--> rx task --> HubPipeline.process
                                                                                      |--> HubPersist queue(32) --> NVS writer task (prio 1)
supervisor task (1 s poll): scanner active and host synced?  10 s no -> restart, 3 failed restarts -> reboot
```

* The callback copies up to 64 bytes and the PDU flag into a queue entry and returns. The timestamp is taken at callback entry
  and travels in the queue entry. If the queue is full the advertisement is counted in `queue_full` (dump only).
* `HubPipeline.process(raw bytes, pdu_nonconn, now_ms)` is the only place that decides. It counts `rx_total` for every call.
  `callbacks seen = rx_total + queue_full`.
* After an ACCEPT the rx task hands `(node, counter)` to the persist queue. NVS is never touched in the callback or in `process`.
  A power cut before the write lands can lose at most the last accepted counter per node (wait 2 s before a reset in tests).
* Scan: passive, window = interval = 100 ms, duplicate filtering OFF (the copies must reach the pipeline).
* Start-up: vest id 0x00 or 0xFF, or no key, means the hub prints `REFUSE` and does not scan. A release build (no `build_opt.h`)
  has no vest id and no key, so it refuses by design until provisioning (S6). Bench builds take the public test key and vest id 0x01
  from `ppe_test_key.h` (`-DHUB_TEST_VEST_ID=0xFF` in `build_opt.h` shows the refusal).
* An unreadable NVS leaves accept disabled (fail safe): only `rx_total` counts.

### NimBLE-Arduino 2.5.1 facts used (read from the library source)

* PDU type: `NimBLEAdvertisedDevice::getAdvType()` returns the legacy event type when extended advertising is off (the default);
  ADV_NONCONN_IND is 3. Payload comes from `getPayload()` unchanged (no rebuilt header).
* Scanner state: `NimBLEScan::isScanning()` is `ble_gap_disc_active()`. Host sync: `ble_hs_synced()` (declared extern "C" in the
  glue because `NimBLEDevice::m_synced` is private). "Healthy" = scanning and synced.
* With `setMaxResults(0)` and duplicates on, every advertisement produces one `onResult`.

## Build and upload (Windows, PowerShell, repo root)

```powershell
# release build (no hooks): compiles, then refuses to run (no provisioning yet)
arduino-cli compile --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc --library common firmware\hub

# bench build with hooks
copy firmware\hub\build_opt.h.bench firmware\hub\build_opt.h
arduino-cli compile --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc --library common firmware\hub
arduino-cli upload  -p COM5 --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc firmware\hub

# bench build with the Wi-Fi recheck: copy build_opt.h.bench_wifi and wifi_secrets.example.h -> wifi_secrets.h (edit it)
```

Delete `firmware\hub\build_opt.h` again before building anything that is meant to be a release. In the Arduino IDE the same file
next to `hub.ino` does the same job. Measured size (core 3.0.7): release 562 KB, bench 566 KB, bench + Wi-Fi 1209 KB (92 % of the
default 1.3 MB app partition; S7 will need a larger partition scheme).

Spare ESP32-C3 sketches: `tools\injector` and `tools\beacon` (board "ESP32C3 Dev Module", USB CDC On Boot enabled, `build_opt.h` is
already next to each sketch). One board is re-flashed between the two roles. The beacon is node 5; inject only nodes 1 to 4 on air
while it runs.

## Serial hooks (bench build only, one command per line, 115200)

| Command | Effect |
|---|---|
| `inject <hex>` | Puts the bytes in the same rx queue and task as a BLE advertisement (any length 0..64, malformed included) |
| `stall <n>` | Stops the scan; the next `n` restart attempts fail (3 = reboot, 1 = recovers on the second restart) |
| `dump` | Registry, `last_counter`, `last_seen`, counters, queue fill, NVS write time, processing time, scan restarts |
| `time` / `time on\|off` | Print the monotonic time / time stamp on every accepted packet (default on; use `time off` for TC-72) |
| `rxlog on\|off` | Every received advertisement before filtering (duplicates included): `RX,<micros at callback>,<hex>` (default off) |

Other log lines: `ACC t_ms=.. node=.. ctr=..` (accepted, with `time on`), `ANOMALY ...` (replay, once per node per 60 s),
`SCAN restart #n ...`, `SCAN reboot ...`, `SCAN quiet_60s ...` (informational), `BOOT ...` (reset reason, registry, previous fault).
Every `dump` line starts with `DUMP ` and uses `key=value`.

## Evidence rule

One serial log per test at `test_logs\S3\<TC>_<sha7>.log` plus one row in `test_logs\summary.csv`
(`TC,sprint,date,sha,result`, append only; on a merge conflict keep both lines). Commit first, then run the final test, so the sha matches the code.

```powershell
python tools\s3_capture.py path --tc TC-22                       # prints the log path (use it as inject.py --log)
python tools\s3_capture.py capture --port COM5 --tc TC-21 --seconds 120 --send-after 5:"stall 1" --send-after 60:dump
python tools\s3_capture.py analyze --tc TC-20 --expected 120 --node 5     # TC-20 and TC-72 pass rules
python tools\s3_capture.py result --tc TC-20 --result PASS --note "119 of 120"
```

Never run two programs on the same COM port (close the IDE serial monitor). When `inject.py --via hub` runs it owns the hub port
and writes its own log; do not start `capture` on that port at the same time.

## Bench runbook

Setup once: hub with the bench build on `<HUB>`; spare C3 on `<INJ>`. Erase the hub NVS before any test that needs fresh counters
(default partition table: NVS at 0x9000, 0x5000 bytes), then press reset:

```powershell
python -m esptool --chip esp32s3 --port <HUB> erase_region 0x9000 0x5000
```

Counter plan so tests do not collide: TC-20 step 1 and TC-24 use nodes 1 to 4 on air; TC-22/23/32 use fresh NVS; TC-72 uses
counters from 100000 up. A beacon reset advances its counter block (a gap of up to 64 is expected after a beacon boot).

**TC-20 continuous scan** (20 min instead of 1 h, 120 beacons, at most 1 miss; CHANGES.md)
1. Flash the C3 with `tools\beacon`, power it next to the hub. Inject nodes 1-4 on air:
   `python tools\inject.py --via injector --port <INJ> pkt --node 1 --ctr 1000 --copies 5` (repeat for nodes 2, 3, 4, `--ctr 1000`).
   Flash the beacon back (node 5 is received from the beacon). `dump`: all 5 nodes show `accepted` above 0.
2. `python tools\s3_capture.py capture --port <HUB> --tc TC-20 --seconds 1260 --send "rxlog on" --send-after 15:dump --send-after 1230:dump`
   (`rxlog on` lets the analysis count how many times each beacon packet was heard.)
3. `python tools\s3_capture.py analyze --tc TC-20 --expected 120 --node 5`, then `result`. The analyze output also prints the per-packet
   duplicate statistics (min, max, average `dup_count` per beacon packet and whether any packet was heard more than once) for S4.

**TC-21 scan watchdog** (beacon running, `time on`)
1. With the beacon (as the test plan says, resolution 10 s): `python tools\s3_capture.py capture --port <HUB> --tc TC-21 --seconds 90 --send "rxlog on" --send-after 5:"stall 1" --send-after 60:dump`.
   For a 1 s resolution use the injector instead (beacon off, injector sketch on the C3) and in a second window
   `python tools\inject.py --via injector --port <INJ> seq --node 2 --ctr-start 3000 --count 60 --interval-ms 1000`.
   Expected log: `PPE_HOOK stall 1`, 10 s later `SCAN restart #1`, `result=failed`, next second `SCAN restart #2`, `result=ok`, then `RX` lines again.
   Report: `python tools\s3_capture.py analyze --tc TC-21` (approved pass rule: first restart attempt 10 to 11 s after the stall, an accepted
   packet again within 10 s of that attempt; it also prints the measured onset to first accepted packet), and no reboot.
2. `stall 3`: `capture ... --seconds 90 --send "stall 3"`. Expected: three `result=failed`, then `SCAN reboot`, the port drops and returns
   (the capture reconnects), `BOOT reset_reason=3` (software reset) and `BOOT previous run ended with fault code 1`.
3. 10 min quiet: beacon and injector unplugged, `capture ... --seconds 600 --send-after 5:dump --send-after 595:dump`. Expected: no `SCAN restart`,
   no new `BOOT`; `scan restarts=0` in the last dump. A room with phones will have traffic, which does not harm the test.
4. Informational: `scan_quiet_60s` in the dump (rises about 1 per minute only where the room is really silent; say so if it did not).

**TC-22 receive filter order** (fresh NVS, hub path so the counters are checked after every packet)
```powershell
python tools\inject.py --via hub --port <HUB> --log (python tools\s3_capture.py path --tc TC-22) --check defects --ctr 300 --malformed --repeat 20
```
Bad length and bad AD header are fed by the hub hook; send the valid-format ones on air as well to see them through the BLE path:
`python tools\inject.py --via injector --port <INJ> defects --ctr 300` (then read `dump`). Pass: every packet raised only its own counter,
`RESULT PASS`, state 0x02 with a valid HMAC counted as `drop_state`, 0 accepted except the valid control.

**TC-23 HMAC** (fresh NVS)
```powershell
python tools\inject.py --via hub --port <HUB> --log (python tools\s3_capture.py path --tc TC-23) --check a1
python tools\inject.py --via hub --port <HUB> --log (python tools\s3_capture.py path --tc TC-23) --check flips
```
Pass: A.1 accepted (node 3, `last_counter` 258), all 136 flips rejected at the stage the spec order predicts, `last_counter` still 258. Repeat the
136 on air with the injector for the end-to-end check.

**TC-24 counter rule and persistence** (fresh NVS, `time on`)
1. 5 copies: `inject.py --via hub --port <HUB> pkt --node 1 --ctr 4000 --copies 5` gives accepted 1, dup 4, one `ACC` line, `last_seen` set once.
   On air (`--via injector ... --copies 5`) each 100 ms copy is heard several times (20-30 ms advertising interval), so `dup_count` is at least 4;
   with `rxlog on` the `RX` lines show 5 bursts about 500 ms apart and still exactly one `ACC` (CHANGES.md).
2. Replays: `inject.py --via hub --port <HUB> pkt --node 1 --ctr 5000`, then `seq --node 1 --ctr-start 4900 --count 100`: `replay_count` 100, accepted unchanged,
   at least one `ANOMALY` line per node per 60 s window.
3. Wait at least 2 s, reset the hub, then `pkt --node 1 --ctr 5000`: rejected as duplicate (c equals L), `accepted` stays 0. Same with `--ctr 4999` (replay).

**TC-25 registry**: send packets for nodes 1-4 (`pkt`, injector or hub) and the beacon for node 5, `dump`, reset the hub, `dump`:
every `last_counter` is retained, all five states `UNKNOWN`, counters and `rx_total` back to 0.

**TC-32 counters** (fresh NVS): `python tools\inject.py --via hub --port <HUB> --log (python tools\s3_capture.py path --tc TC-32) --check tc32`.
The mix is 10 bad length, 7 wrong version, 12 wrong vest, 9 unknown node, 15 bad HMAC, 6 valid HMAC with invalid state, 20 duplicates, 8 replays,
30 accepted (117 in total); the tool checks the delta after every packet and the final totals. Also check `callbacks_seen` equals `rx_total` plus `queue_full`.

**TC-72 processing time** (fresh NVS, then `time off`; `rxlog` stays off)
1. `python tools\inject.py --via hub --port <HUB> --log (python tools\s3_capture.py path --tc TC-72) --pre "time off" --dump-at-end seq --node 1 --ctr-start 100000 --count 1000`
   (1000 accepted packets, so 1000 NVS write requests; `time off` keeps the serial printing out of the timing).
2. Reset the hub, then the burst: `... --pre "time off" --dump-at-end burst --ctr-start 200000` (5 nodes x 5 copies written in one go). Expected in the dump: `rx_total` 25,
   `queue_full` 0, every node accepted 1 and dup 4.
3. The dump shows `persist ... last_us max_us avg_us` (the NVS write time) and `proc ... max_us over_10ms`. Effect of the writes: the timing stats are since boot, so reset
   the hub and repeat step 1 once more with a counter range that is already stored (`--ctr-start 100000` again gives duplicates and no NVS write); compare the two `proc max_us`.
   Pass: max 10000 us or less over 1000 packets, 25 of 25 processed. `python tools\s3_capture.py analyze --tc TC-72` reads the last dump of the log.

**Wi-Fi + BLE recheck 1**: build with `build_opt.h.bench_wifi` and your `wifi_secrets.h`, start `python tools\gateway_stub.py --port 8080 --log test_logs\S3\gateway_stub_arrivals.log`
(allow Python through the Windows firewall for private networks), repeat the TC-20 run with `--label wifi` and
`python tools\s3_capture.py analyze --tc TC-20 --expected 120 --node 5 --recheck`. Pass: same 99 % rule and `gap_count` growth equals the misses; the stub shows one arrival
about every 30 s and the log shows `WIFI http code=200`.

## Build figures for planning (sprint S3 build, arduino-cli 1.5.2-rc.1 with esp32 core 3.0.7 and NimBLE-Arduino 2.5.1)

FQBN for every hub build: `esp32:esp32:esp32s3:CDCOnBoot=cdc`. Partition scheme: `default` (the core's
"Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)", flash 4 MB, app maximum 1310720 bytes). The bench and Wi-Fi builds
need the same scheme; no custom partition table exists in the repo yet.

| Build | Flags | Flash used | Flash % of 1310720 | RAM (static) | RAM % of 327680 |
|---|---|---|---|---|---|
| Release | none (no `build_opt.h`) | 562453 bytes | 42 % | 30124 bytes | 9 % |
| Bench | `-DPPE_TEST_HOOKS` | 566301 bytes | 43 % | 30388 bytes | 9 % |
| Bench + Wi-Fi recheck | `-DPPE_TEST_HOOKS -DPPE_S3_WIFI_RECHECK` | 1209441 bytes | 92 % | 49992 bytes | 15 % |

Measured with `arduino-cli compile --warnings more`. Static RAM only; heap at run time is not included.
The Wi-Fi build is over the 90 % mark: the S7 uplink will need a larger app partition or the Wi-Fi code trimmed.

## Known limits (open for later sprints)

* Node state is only `UNKNOWN`/`NOT_WORN`/`WORN` here; OFFLINE, compliance, motor and `HUB_FAULT` are S5/S7 (the scan-stall reboot only leaves a fault code in RTC memory and a log line).
* Persisting after dispatch means a reset within about 2 s of the last accepted packet can forget that counter (spec 6.2); a lost write is counted (`write_fails`, `dropped`) and not retried in a loop.
* The pipeline, registry and counters are protected by one mutex that the rx task holds only while a packet is processed.
