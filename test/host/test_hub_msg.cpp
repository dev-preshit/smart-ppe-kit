// S5: the serial line of each hub message (D3). The S5 evidence for the message part of TC-27, TC-28, TC-30 and TC-31 is
// this line (FR-30 allows the serial log when no uplink exists); the queue, hub_seq and JSON are S7.
// Expected strings are written out from protocol spec 10 / 10.1 field names.
#include "ppe_test.h"
#include <string>
#include "hub_msg.h"

static std::string fmt(const HubMsg& m) { char b[200]; size_t n = hub_msg_format(m, b, sizeof b); CHECK_EQ(n, strlen(b)); return b; }
static HubMsg mk(uint8_t type) { HubMsg m; memset(&m, 0, sizeof m); m.type = type; m.vest_id = 1; return m; }

PPE_TEST(compliance_line_lists_the_nodes_not_ok, "TC-28") {
  HubMsg m = mk(HUB_MSG_COMPLIANCE); m.t_ms = 1234;
  m.compliant = 0; m.nodes_not_ok = (1u << 3) | (1u << 5);
  CHECK(fmt(m) == "MSG msg_type=COMPLIANCE vest_id=1 state=NON_COMPLIANT nodes_not_ok=[3,5] t_ms=1234");
  m.compliant = 1; m.nodes_not_ok = 0;
  CHECK(fmt(m) == "MSG msg_type=COMPLIANCE vest_id=1 state=COMPLIANT nodes_not_ok=[] t_ms=1234");
  m.compliant = 0; m.nodes_not_ok = 0x3E;
  CHECK(fmt(m) == "MSG msg_type=COMPLIANCE vest_id=1 state=NON_COMPLIANT nodes_not_ok=[1,2,3,4,5] t_ms=1234");
}

PPE_TEST(node_offline_line, "TC-27") {
  HubMsg m = mk(HUB_MSG_NODE_OFFLINE); m.node_id = 3; m.last_seen_age_s = 780; m.t_ms = 900000;
  CHECK(fmt(m) == "MSG msg_type=NODE_OFFLINE vest_id=1 node_id=3 state=OFFLINE last_seen_age_s=780 t_ms=900000");
}

PPE_TEST(hub_boot_line, "TC-31") {
  HubMsg m = mk(HUB_MSG_HUB_BOOT); m.reset_reason = 3; m.t_ms = 12;
  CHECK(fmt(m) == "MSG msg_type=HUB_BOOT vest_id=1 reset_reason=3 fw=s5-hub-logic t_ms=12");
}

PPE_TEST(hub_fault_line_every_code, "TC-30") {
  const struct { uint8_t code; const char* name; } t[] = {
    {HUB_FAULT_SCAN_STALL, "SCAN_STALL"}, {HUB_FAULT_WDT_RESET, "WDT_RESET"}, {HUB_FAULT_NVS_READ, "NVS_READ"},
    {HUB_FAULT_NVS_WRITE, "NVS_WRITE"}, {HUB_FAULT_NO_KEY, "NO_KEY"}, {HUB_FAULT_GUARD, "GUARD"}, {HUB_FAULT_REGISTRY, "REGISTRY"}};
  for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
    HubMsg m = mk(HUB_MSG_HUB_FAULT); m.fault_code = t[i].code; m.t_ms = 5;
    CHECK(fmt(m) == std::string("MSG msg_type=HUB_FAULT vest_id=1 code=") + t[i].name + " t_ms=5");   // spec 10.1 code names
  }
}

PPE_TEST(format_is_safe_for_tiny_buffers, "TC-28") {
  HubMsg m = mk(HUB_MSG_COMPLIANCE); char b[8];
  size_t n = hub_msg_format(m, b, sizeof b);
  CHECK_EQ(n, 7); CHECK_EQ(strlen(b), 7);
  CHECK_EQ(hub_msg_format(m, nullptr, 10), 0); CHECK_EQ(hub_msg_format(m, b, 0), 0);
}
