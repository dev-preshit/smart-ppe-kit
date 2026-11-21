#!/usr/bin/env python3
"""S5 hub logic mutants (TC-26, TC-27, TC-28): reuses the engine of tools/mutation_check.py with its own list, so
the shared list stays untouched. Applies one deliberate bug at a time to a scratch copy and requires `make test`
to report at least one failing test. Exit 1 if any mutant survives. Run: python3 tools/mutation_check_s5.py"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mutation_check

P = "firmware/hub/src/hub_pipeline.cpp"
H = "firmware/hub/src/hub_pipeline.h"
C = "firmware/hub/src/hub_config.h"
M = "firmware/hub/src/hub_motor.cpp"
MSGF = "firmware/hub/src/hub_msg.cpp"
FAULTS = [  # S5 part 2: hub fault handling (TC-30)
 ("s5f: NO_KEY never reported", MSGF, "  if (!b.key_ok) out[n++] = HUB_FAULT_NO_KEY;", "  if (false) out[n++] = HUB_FAULT_NO_KEY;"),
 ("s5f: REGISTRY reported without the key too", MSGF, "  else if (!b.registry_ok) out[n++] = HUB_FAULT_REGISTRY;", "  if (!b.registry_ok) out[n++] = HUB_FAULT_REGISTRY;"),
 ("s5f: watchdog reset not reported", MSGF, "  else if (b.wdt_reset) out[n++] = HUB_FAULT_WDT_RESET;", "  else if (false) out[n++] = HUB_FAULT_WDT_RESET;"),
 ("s5f: stored scan-stall fault not reported", MSGF, "  if (b.rtc_fault) out[n++] = b.rtc_fault;", "  if (false) out[n++] = b.rtc_fault;"),
 ("s5f: NVS_READ never reported", MSGF, "  if (!b.params_ok) out[n++] = HUB_FAULT_NVS_READ;", "  if (false) out[n++] = HUB_FAULT_NVS_READ;"),
 ("s5f: no-key start accepts an invalid vest id", P, "  if (hub_startup_check(vest_id, true) != HUB_START_OK) return false;   // the vest id must still be valid\n", ""),
 ("s5f: no-key start leaves accept enabled", P, "  accept_enabled_ = key_ok_ && registry_ok_;", "  accept_enabled_ = registry_ok_;"),
 ("s5f: fault hook name nvs-read mismapped", "firmware/hub/src/hub_hooks.cpp", '{"nvs-read", 2}', '{"nvs-read", 3}'),
]
MOTOR = [  # S5 part 2: motor FSM (TC-29, TC-30, TC-74)
 ("s5m: alert ON 400 ms instead of 300", C, "HUB_MOTOR_ON_US        300000u", "HUB_MOTOR_ON_US        400000u"),
 ("s5m: alert OFF 600 ms instead of 700", C, "HUB_MOTOR_OFF_US       700000u", "HUB_MOTOR_OFF_US       600000u"),
 ("s5m: guard at 500 ms instead of 395", C, "HUB_MOTOR_GUARD_US     395000u", "HUB_MOTOR_GUARD_US     500000u"),
 ("s5m: key pattern with 2 pulses", C, "HUB_KEY_PULSES         3u", "HUB_KEY_PULSES         2u"),
 ("s5m: key rest 1500 ms", C, "HUB_KEY_REST_US       1700000u", "HUB_KEY_REST_US       1500000u"),
 ("s5m: guard never acts", M, "if (!guard_armed_ || !due(now_us, guard_dl_)) return;", "return;"),
 ("s5m: guard not armed by an ON edge", M, "    guard_armed_ = true;\n  } else {", "  } else {"),
 ("s5m: alert clear does not stop the motor at once", M, "    set_out(false, now_us);                        // M-06: low at once, timers cancelled\n", ""),
 ("s5m: late step bursts instead of resync", M, "(late > HUB_MOTOR_RESYNC_US) ? now_us : dl_", "((late > HUB_MOTOR_RESYNC_US) ? dl_ : dl_)"),
 ("s5m: boot leaves the output as it was", M, "  out_ = false; alert_ = false; key_ = false;", "  alert_ = false; key_ = false;"),
 ("s5m: deadline compare not wrap safe", M, "return (int32_t)(now - deadline) >= 0;", "return now >= deadline;"),
 ("s5m: guard trip never reported", M, "  trip_pending_ = true;\n", ""),
 ("s5m: re-alert while idle does not start", M, "  if (active && state_ == M_IDLE) {", "  if (false) {"),
]

mutation_check.MUTANTS = [  # (name, file, old, new)
 ("s5: timeout is >= instead of >", P, "if (age_ms <= timeout_ms) continue;", "if (age_ms < timeout_ms) continue;"),
 ("s5: OFFLINE message repeats (H-13)", P, "if (n.state == HUB_NODE_OFFLINE) continue;      // H-13", "if (false) continue;      // H-13"),
 ("s5: no compliance check on accept", P, "      eval_compliance(now_ms);                       // X-R1", "      // eval_compliance(now_ms);                       // X-R1"),
 ("s5: UNKNOWN starts the alert", P, "if (node_[id].state == HUB_NODE_NOT_WORN || node_[id].state == HUB_NODE_OFFLINE) return true;", "if (node_[id].state != HUB_NODE_WORN) return true;"),
 ("s5: OFFLINE does not start the alert", P, "if (node_[id].state == HUB_NODE_NOT_WORN || node_[id].state == HUB_NODE_OFFLINE) return true;", "if (node_[id].state == HUB_NODE_NOT_WORN) return true;"),
 ("s5: compliance message without a change", P, "if (now_ok == vest_compliant_) return;            // C-04: no change, no message", "if (false) return;"),
 ("s5: boot starts COMPLIANT", P, "  vest_compliant_ = false;          // C-01", "  vest_compliant_ = true;          // C-01"),
 ("s5: margin ignored in the timeout", H, "return HUB_OFFLINE_MISSED * heartbeat_s_ + margin_s_;", "return HUB_OFFLINE_MISSED * heartbeat_s_;"),
 ("s5: one missed heartbeat is enough", H, "return HUB_OFFLINE_MISSED * heartbeat_s_ + margin_s_;", "return heartbeat_s_ + margin_s_;"),
 ("s5: margin bound off by one", P, "margin_s > HUB_MARGIN_S_MAX", "margin_s > HUB_MARGIN_S_MAX + 1"),
 ("s5: heartbeat lower bound off by one", P, "heartbeat_s < HUB_HEARTBEAT_S_MIN", "heartbeat_s < HUB_HEARTBEAT_S_MIN - 1"),
 ("s5: clock behind last_seen gives OFFLINE", P, "    if (diff <= 0) continue;\n", ""),
 ("s5: boot does not clear queued messages", P, "  msg_n_ = 0;                       // a restart", "  // msg_n_ = 0;                       // a restart"),
 ("s5: queue overflow keeps the newest out", P, "    for (uint32_t i = 1; i < msg_n_; i++) msgq_[i - 1] = msgq_[i];\n    msg_n_--;\n    msg_dropped_++;", "    return;"),
]
mutation_check.MUTANTS = mutation_check.MUTANTS + MOTOR + FAULTS
if __name__ == "__main__": sys.exit(mutation_check.main())
