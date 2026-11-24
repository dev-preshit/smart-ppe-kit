// S5 part 2, host part of TC-30 (hub fault handling): the boot fault order (B-05, X-07), the no-key start (B-04), the
// unreadable-registry start (B-04b) and the `fault` hook parser. The bench runs with the motor pin recording are separate.
// Expected values come from state machines 2.1 / 2.9 (B-04, B-04b, B-05, X-01..X-07), FR-30 and S0 finding V12-03.
#include "ppe_test.h"
#include "hub_test_util.h"
#include "hub_hooks.h"
#include "hub_msg.h"

PPE_TEST(fault_hook_parser_knows_the_six_kinds, "TC-30") {
  const struct { const char* line; uint32_t id; } ok[] = {{"fault wdt", 1}, {"fault nvs-read", 2}, {"fault nvs-write", 3},
                                                          {"fault registry", 4}, {"fault key", 5}, {"fault guard", 6}, {"  fault   key \r\n", 5}};
  for (unsigned i = 0; i < sizeof ok / sizeof ok[0]; i++) {
    HubHookCmd c = hub_hook_parse(ok[i].line);
    CHECK_EQ(c.type, HUB_HOOK_FAULT); CHECK_EQ(c.val, ok[i].id);
  }
  const char* bad[] = {"fault", "fault x", "fault wdt 1", "fault nvs", "fault keys", "fault Key", "faults key", "fault nvs-read nvs-write"};
  for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) CHECK_EQ(hub_hook_parse(bad[i]).type, HUB_HOOK_ERROR);
}

PPE_TEST(boot_fault_order_and_selection, "TC-30") {
  uint8_t f[4];
  HubBootInfo clean = {0, false, true, true, true};
  CHECK_EQ(hub_boot_faults(clean, f), 0);                                          // nothing to report after a normal start
  HubBootInfo stall = {HUB_FAULT_SCAN_STALL, false, true, true, true};
  CHECK_EQ(hub_boot_faults(stall, f), 1); CHECK_EQ(f[0], HUB_FAULT_SCAN_STALL);    // X-01 + X-07
  HubBootInfo wdt = {0, true, true, true, true};
  CHECK_EQ(hub_boot_faults(wdt, f), 1); CHECK_EQ(f[0], HUB_FAULT_WDT_RESET);       // X-02
  HubBootInfo both = {HUB_FAULT_SCAN_STALL, true, true, true, true};
  CHECK_EQ(hub_boot_faults(both, f), 1); CHECK_EQ(f[0], HUB_FAULT_SCAN_STALL);     // the stored code wins, one report per reboot
  HubBootInfo nokey = {0, false, false, true, true};
  CHECK_EQ(hub_boot_faults(nokey, f), 1); CHECK_EQ(f[0], HUB_FAULT_NO_KEY);        // X-04
  HubBootInfo noreg = {0, false, true, false, true};
  CHECK_EQ(hub_boot_faults(noreg, f), 1); CHECK_EQ(f[0], HUB_FAULT_REGISTRY);      // X-03 / V12-03 (b)
  HubBootInfo nokey_noreg = {0, false, false, false, true};
  CHECK_EQ(hub_boot_faults(nokey_noreg, f), 1); CHECK_EQ(f[0], HUB_FAULT_NO_KEY);  // B-04: REGISTRY only with the key present
  HubBootInfo params = {0, false, true, true, false};
  CHECK_EQ(hub_boot_faults(params, f), 1); CHECK_EQ(f[0], HUB_FAULT_NVS_READ);     // X-03: any other read, V12-03 (b)
  HubBootInfo many = {HUB_FAULT_SCAN_STALL, false, false, true, false};
  CHECK_EQ(hub_boot_faults(many, f), 3);
  CHECK_EQ(f[0], HUB_FAULT_SCAN_STALL); CHECK_EQ(f[1], HUB_FAULT_NO_KEY); CHECK_EQ(f[2], HUB_FAULT_NVS_READ);   // B-05 order
}

PPE_TEST(no_key_start_accepts_nothing_but_the_nodes_still_time_out, "TC-30") {
  HubPipeline h; FakeHubStore st;
  CHECK(h.init_no_key(1)); CHECK(!h.key_ok());
  CHECK(h.load_registry(&st, 5000)); CHECK(!h.accept_enabled());                   // B-04: accept disabled although the registry is fine
  uint8_t adv[21]; hub_adv(2, PPE_WORN, 1, adv);
  CHECK_EQ(h.process(adv, 21, true, 6000).verdict, HUB_V_DROP_DISABLED);           // every packet dropped, even a valid one
  CHECK_EQ(h.node(2).state, HUB_NODE_UNKNOWN); CHECK_EQ(h.global().rx_total, 1);
  for (uint32_t t = 1000; t <= 790000; t += 1000) h.tick(5000 + t);
  for (uint8_t n = 1; n <= 5; n++) CHECK_EQ(h.node(n).state, HUB_NODE_OFFLINE);    // slots with timers from boot (B-04)
  CHECK(h.alert_active());                                                          // the decision stays true; the motor FSM lets KEY_FAULT win (X-R7)
}

PPE_TEST(no_key_start_refuses_an_invalid_vest_id, "TC-30") {
  HubPipeline h;
  CHECK(!h.init_no_key(0)); CHECK(!h.init_no_key(0xFF)); CHECK(h.init_no_key(0x01));
}

PPE_TEST(unreadable_registry_disables_accept_and_every_node_goes_offline_within_785_s, "TC-30") {
  FakeHubStore st; st.read_error = true;
  HubPipeline h; CHECK(h.init(1, V_KEY));
  CHECK(!h.load_registry(&st, 100000)); CHECK(!h.registry_ok()); CHECK(!h.accept_enabled());
  uint8_t adv[21]; hub_adv(3, PPE_WORN, 5, adv);
  CHECK_EQ(h.process(adv, 21, true, 101000).verdict, HUB_V_DROP_DISABLED);
  uint32_t first_offline = 0;
  for (uint32_t t = 1000; t <= 800000 && !first_offline; t += 1000) {
    h.tick(100000 + t);
    bool all = true; for (uint8_t n = 1; n <= 5; n++) if (h.node(n).state != HUB_NODE_OFFLINE) all = false;
    if (all) first_offline = t;
  }
  CHECK(first_offline >= 780000u && first_offline <= 785000u);                      // V12-04, B-04b
  CHECK(h.alert_active());
}

PPE_TEST(a_null_store_is_the_same_as_an_unreadable_registry, "TC-30") {
  HubPipeline h; CHECK(h.init(1, V_KEY));
  CHECK(!h.load_registry(nullptr, 0)); CHECK(!h.accept_enabled());
}

PPE_TEST(fault_messages_queue_in_boot_order_after_hub_boot, "TC-30") {
  HubPipeline h; CHECK(h.init_no_key(1)); FakeHubStore st; h.load_registry(&st, 0);
  h.note_boot(3, 0);
  HubBootInfo bi = {HUB_FAULT_SCAN_STALL, false, false, true, true};
  uint8_t f[4];
  for (uint32_t i = 0, n = hub_boot_faults(bi, f); i < n; i++) h.note_fault(f[i], 0);
  std::vector<HubMsg> v = hub_drain(h);
  CHECK_EQ(v.size(), 3); if (v.size() != 3) return;
  CHECK_EQ(v[0].type, HUB_MSG_HUB_BOOT);                                            // HUB_BOOT first (B-05)
  CHECK_EQ(v[1].type, HUB_MSG_HUB_FAULT); CHECK_EQ(v[1].fault_code, HUB_FAULT_SCAN_STALL);
  CHECK_EQ(v[2].type, HUB_MSG_HUB_FAULT); CHECK_EQ(v[2].fault_code, HUB_FAULT_NO_KEY);
}
