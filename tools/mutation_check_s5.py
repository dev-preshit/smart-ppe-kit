#!/usr/bin/env python3
"""S5 hub logic mutants (TC-26, TC-27, TC-28): reuses the engine of tools/mutation_check.py with its own list, so
the shared list stays untouched. Applies one deliberate bug at a time to a scratch copy and requires `make test`
to report at least one failing test. Exit 1 if any mutant survives. Run: python3 tools/mutation_check_s5.py"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mutation_check

P = "firmware/hub/src/hub_pipeline.cpp"
H = "firmware/hub/src/hub_pipeline.h"
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
if __name__ == "__main__": sys.exit(mutation_check.main())
