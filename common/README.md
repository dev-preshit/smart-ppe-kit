# ppe_common - Smart PPE shared core (S1)

One copy of the wire format and the logic both firmwares share. Pure C++11, no heap, no Arduino in the core
(only `ppe_log.cpp` touches `Serial`, behind `#ifdef ARDUINO`). Implements `docs/protocol_spec_v1.1.md`.

| Header | What it gives you | Spec / SRS |
|---|---|---|
| `ppe_proto.h` | constants, flag bits, node ids, `ppe_packet_t`, encode/decode, 21-byte adv builder, `static_assert`s for the sizes | spec 1-3, NFR-10 |
| `ppe_crypto.h` | `ppe_sign_payload`, `ppe_hmac_verify` (mbedtls, 8-byte tag, constant-time compare) | spec 5, FR-12, FR-23 |
| `ppe_filter.h` | `ppe_rx_filter` (spec 2 steps 1-6, in order) and `ppe_counter_check` (step 7) | FR-22, FR-24 |
| `ppe_counter.h` | `PpeNodeCounter` over a `PpeCounterStore` interface: cold boot, resume, next, NODE_FAULT causes | spec 6.1, FR-11, FR-17 |
| `ppe_log.h` | `PPE_LOGE/W/I/D(tag, fmt, ...)`, level `-DPPE_LOG_LEVEL=0..4`, replaceable sink | |
| `ppe_test_key.h` | the public Appendix A test key. Compiles only with `-DPPE_TEST_HOOKS` | spec App. A |
| `secrets.example.h` | placeholder only (see "Keys" below) | FR-42 |

Include everything with `#include <ppe_common.h>`.

## Use from the Arduino IDE 2.x on Windows

The library is this folder (`common/`, with `library.properties` and `src/`). Link it, do not copy it, so every sketch
always sees the one current copy. A directory junction needs no administrator rights.

1. Close the Arduino IDE.
2. Open **Command Prompt** (not PowerShell) and run, replacing `<repo>` with your clone path (quotes are needed if it contains spaces):

       mklink /J "%USERPROFILE%\Documents\Arduino\libraries\ppe_common" "<repo>\common"

   If your sketchbook folder is elsewhere (IDE: File > Preferences > Sketchbook location), use `<sketchbook>\libraries\ppe_common`.
3. Start the IDE. In a sketch use `#include <ppe_common.h>`. Check: File > Examples shows no error and Sketch > Include Library lists `ppe_common`.
4. To unlink: `rmdir "%USERPROFILE%\Documents\Arduino\libraries\ppe_common"`. Never use `rd /s` or `del /s` on it, those can follow the junction and delete the repo files.

Test builds that need the test key: add a file `build_opt.h` next to the sketch containing `-DPPE_TEST_HOOKS`.
Release builds must not have it (TC-78 checks this).

arduino-cli does not need the junction: `arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc --library "<repo>\common" "<sketch>"`.
Do not use "Add .ZIP Library": it makes a stale copy.

## Keys (SRS FR-42)

The real vest key is never in source, build files or any commit. It reaches a device by serial provisioning from a file outside the repo
(`tools/provision.py`, a later sprint). Until then bench builds use the public test key (`ppe_test_key.h`).
`secrets.example.h` is a placeholder: copy it to `common/src/secrets.h` (gitignored) only for a throwaway development key.
`tools/secret_scan.sh --key-file <real key file>` scans every commit **and the working tree including ignored files**.

## Rules the modules rely on (S2-S5 must keep them)

- The counter is only handed out by `PpeNodeCounter::next()`. If it returns an error, do not transmit (NODE_FAULT).
- On a deep-sleep wake call `resume(counter, ceiling)` from RTC memory. Only a real boot calls `cold_boot()`.
- `ppe_rx_filter` takes `pdu_is_nonconn_adv` from the BLE glue. A `PPE_RX_HMAC_FAIL` result must not touch the counter or the node state.
- Never pass key bytes to `PPE_LOG*`.
