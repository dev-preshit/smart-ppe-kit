#include "hub_msg.h"
#include <stdio.h>
#include "hub_config.h"

const char* hub_fault_code_name(uint8_t code) {
  switch (code) {
    case HUB_FAULT_SCAN_STALL: return "SCAN_STALL";
    case HUB_FAULT_WDT_RESET: return "WDT_RESET";
    case HUB_FAULT_NVS_READ: return "NVS_READ";
    case HUB_FAULT_NVS_WRITE: return "NVS_WRITE";
    case HUB_FAULT_NO_KEY: return "NO_KEY";
    case HUB_FAULT_GUARD: return "GUARD";
    case HUB_FAULT_REGISTRY: return "REGISTRY";
    default: return "?";
  }
}

uint32_t hub_boot_faults(const HubBootInfo& b, uint8_t out[4]) {
  uint32_t n = 0;
  if (b.rtc_fault) out[n++] = b.rtc_fault;                    // X-07: stored by the run that rebooted (SCAN_STALL)
  else if (b.wdt_reset) out[n++] = HUB_FAULT_WDT_RESET;       // X-02: from the reset reason, nothing is stored
  if (!b.key_ok) out[n++] = HUB_FAULT_NO_KEY;                 // B-04
  else if (!b.registry_ok) out[n++] = HUB_FAULT_REGISTRY;     // B-04b: only with the key present
  if (!b.params_ok) out[n++] = HUB_FAULT_NVS_READ;            // X-03: a non-critical read failed twice
  return n;
}

size_t hub_msg_format(const HubMsg& m, char* out, size_t cap) {
  if (!out || cap == 0) return 0;
  const unsigned long t = (unsigned long)m.t_ms;
  const unsigned vest = (unsigned)m.vest_id;
  int n = 0;
  switch (m.type) {
    case HUB_MSG_COMPLIANCE: {
      char list[24];
      size_t k = 0;
      list[k++] = '[';
      bool first = true;
      for (uint8_t id = 1; id <= HUB_NODE_SLOTS; id++) {
        if (m.nodes_not_ok & (1u << id)) {
          if (!first) list[k++] = ',';
          list[k++] = (char)('0' + id);
          first = false;
        }
      }
      list[k++] = ']';
      list[k] = 0;
      n = snprintf(out, cap, "MSG msg_type=COMPLIANCE vest_id=%u state=%s nodes_not_ok=%s t_ms=%lu", vest,
                   m.compliant ? "COMPLIANT" : "NON_COMPLIANT", list, t);
      break;
    }
    case HUB_MSG_NODE_OFFLINE:
      n = snprintf(out, cap, "MSG msg_type=NODE_OFFLINE vest_id=%u node_id=%u state=OFFLINE last_seen_age_s=%lu t_ms=%lu", vest,
                   (unsigned)m.node_id, (unsigned long)m.last_seen_age_s, t);
      break;
    case HUB_MSG_HUB_BOOT:
      n = snprintf(out, cap, "MSG msg_type=HUB_BOOT vest_id=%u reset_reason=%lu fw=%s t_ms=%lu", vest,
                   (unsigned long)m.reset_reason, HUB_FW_ID, t);
      break;
    case HUB_MSG_HUB_FAULT:
      n = snprintf(out, cap, "MSG msg_type=HUB_FAULT vest_id=%u code=%s t_ms=%lu", vest, hub_fault_code_name(m.fault_code), t);
      break;
    default:
      n = snprintf(out, cap, "MSG msg_type=? t_ms=%lu", t);
      break;
  }
  if (n < 0) { out[0] = 0; return 0; }
  return (size_t)n >= cap ? cap - 1 : (size_t)n;
}
