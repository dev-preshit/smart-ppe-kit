// Hub-generated messages (protocol spec 10, 10.1) and the sink interface. Pure C++, no Arduino.
// S5 scope: COMPLIANCE, NODE_OFFLINE, HUB_BOOT, HUB_FAULT. In S5 the sink prints one serial line per message
// (hub_msg_format). The uplink queue, hub_seq and JSON are S7: S7 plugs a queue-pushing HubSink into the same
// call site (hub_ble.cpp hub_set_sink) without changing the S5 code.
#pragma once
#include <stddef.h>
#include <stdint.h>

enum HubMsgType { HUB_MSG_NONE = 0, HUB_MSG_COMPLIANCE, HUB_MSG_NODE_OFFLINE, HUB_MSG_HUB_BOOT, HUB_MSG_HUB_FAULT };

// HUB_FAULT codes (spec 10.1)
enum HubFaultCode {
  HUB_FAULT_NONE = 0, HUB_FAULT_SCAN_STALL, HUB_FAULT_WDT_RESET, HUB_FAULT_NVS_READ, HUB_FAULT_NVS_WRITE,
  HUB_FAULT_NO_KEY, HUB_FAULT_GUARD, HUB_FAULT_REGISTRY
};

struct HubMsg {
  uint8_t  type;             // HubMsgType
  uint8_t  vest_id;
  uint8_t  node_id;          // NODE_OFFLINE
  uint8_t  compliant;        // COMPLIANCE: 1 = COMPLIANT, 0 = NON_COMPLIANT
  uint8_t  nodes_not_ok;     // COMPLIANCE: bit n set = node id n is not WORN (bits 1..5)
  uint8_t  fault_code;       // HUB_FAULT: HubFaultCode
  uint32_t t_ms;             // hub monotonic time when the message was generated (the hub keeps no wall clock, D-08)
  uint32_t last_seen_age_s;  // NODE_OFFLINE
  uint32_t reset_reason;     // HUB_BOOT
};

class HubSink {
 public:
  virtual ~HubSink() {}
  virtual void emit(const HubMsg& m) = 0;
};

const char* hub_fault_code_name(uint8_t code);
// One line, field names from spec 10.1. Returns the length written (0 for an empty buffer), always NUL terminated.
size_t hub_msg_format(const HubMsg& m, char* out, size_t cap);
