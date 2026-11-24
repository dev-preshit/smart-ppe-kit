// S5 TC-28: compliance engine (FR-28, NFR-13, state machines C-01..C-04) and the alert decision (D-10).
// Host test, simulated clock. Expected values come from the SRS text and the spec 10.1 message fields.
#include "ppe_test.h"
#include "hub_test_util.h"

static unsigned count_type(const std::vector<HubMsg>& v, uint8_t type) { unsigned n = 0; for (size_t i = 0; i < v.size(); i++) if (v[i].type == type) n++; return n; }

PPE_TEST(all_five_worn_is_compliant_with_exactly_one_message, "TC-28") {
  HubPipeline h; FakeHubStore st; hub_boot(h, st, 0);
  for (uint8_t n = 1; n <= 4; n++) { hub_send(h, n, PPE_WORN, 1, 100 * n); CHECK(!h.vest_compliant()); }
  CHECK_EQ(h.messages_pending(), 0);                           // four WORN and one UNKNOWN: still non-compliant, no transition
  hub_send(h, 5, PPE_WORN, 1, 600);
  CHECK(h.vest_compliant());
  std::vector<HubMsg> v = hub_drain(h);
  CHECK_EQ(v.size(), 1); if (v.size() != 1) return; CHECK_EQ(v[0].type, HUB_MSG_COMPLIANCE); CHECK_EQ(v[0].compliant, 1); CHECK_EQ(v[0].nodes_not_ok, 0); CHECK_EQ(v[0].t_ms, 600);
  CHECK(!h.alert_active());
}

// For each node in turn: NOT_WORN, OFFLINE and UNKNOWN each give NON_COMPLIANT (FR-28 acceptance criterion).
PPE_TEST(each_node_not_worn_gives_non_compliant_one_message, "TC-28") {
  for (uint8_t n = 1; n <= 5; n++) {
    HubPipeline h; FakeHubStore st; hub_boot(h, st, 0);
    for (uint8_t k = 1; k <= 5; k++) hub_send(h, k, PPE_WORN, 1, 10);
    hub_drain(h);
    hub_send(h, n, PPE_NOT_WORN, 2, 20);
    CHECK(!h.vest_compliant());
    std::vector<HubMsg> v = hub_drain(h);
    CHECK_EQ(v.size(), 1); if (v.size() != 1) return; CHECK_EQ(v[0].type, HUB_MSG_COMPLIANCE); CHECK_EQ(v[0].compliant, 0); CHECK_EQ(v[0].nodes_not_ok, (1u << n));
    CHECK(h.alert_active());
  }
}

PPE_TEST(each_node_offline_gives_non_compliant_one_message, "TC-28") {
  for (uint8_t n = 1; n <= 5; n++) {
    HubPipeline h; FakeHubStore st; hub_boot(h, st, 0);
    for (uint8_t k = 1; k <= 5; k++) hub_send(h, k, PPE_WORN, 1, 0);
    hub_drain(h);
    hub_run_ticks(h, 1000, 499000);
    for (uint8_t k = 1; k <= 5; k++) if (k != n) hub_send(h, k, PPE_WORN, 2, 500000);   // the others stay alive
    hub_run_ticks(h, 501000, 790000);
    CHECK_EQ(h.node(n).state, HUB_NODE_OFFLINE);
    CHECK(!h.vest_compliant());
    std::vector<HubMsg> v = hub_drain(h);
    CHECK_EQ(count_type(v, HUB_MSG_COMPLIANCE), 1);
    CHECK_EQ(count_type(v, HUB_MSG_NODE_OFFLINE), 1);
    for (size_t i = 0; i < v.size(); i++) if (v[i].type == HUB_MSG_COMPLIANCE) { CHECK_EQ(v[i].compliant, 0); CHECK_EQ(v[i].nodes_not_ok, (1u << n)); }
    CHECK(h.alert_active());
  }
}

PPE_TEST(each_node_unknown_gives_non_compliant_without_alert_and_without_message, "TC-28") {
  for (uint8_t n = 1; n <= 5; n++) {
    HubPipeline h; FakeHubStore st; hub_boot(h, st, 0);
    for (uint8_t k = 1; k <= 5; k++) if (k != n) hub_send(h, k, PPE_WORN, 1, 10);
    CHECK_EQ(h.node(n).state, HUB_NODE_UNKNOWN);
    CHECK(!h.vest_compliant());                                // UNKNOWN is non-compliant (NFR-13)
    CHECK(!h.alert_active());                                  // but never starts the alert (D-04, D-10)
    CHECK_EQ(h.nodes_not_ok_mask(), (1u << n));
    CHECK_EQ(h.messages_pending(), 0);                         // the boot NON_COMPLIANT is not a transition
  }
}

PPE_TEST(no_compliance_message_at_hub_boot_or_after_a_restart, "TC-28") {
  HubPipeline h; FakeHubStore st; hub_boot(h, st, 0);
  h.note_boot(3, 0);
  std::vector<HubMsg> v = hub_drain(h);
  CHECK_EQ(v.size(), 1); if (v.size() != 1) return; CHECK_EQ(v[0].type, HUB_MSG_HUB_BOOT);   // only HUB_BOOT, no COMPLIANCE
  for (uint8_t k = 1; k <= 5; k++) hub_send(h, k, PPE_WORN, 1, 10);
  CHECK(h.vest_compliant()); hub_drain(h);
  CHECK(h.load_registry(&st, 99));                              // reset while COMPLIANT: boot state is NON_COMPLIANT again
  CHECK(!h.vest_compliant());
  h.note_boot(1, 99);
  v = hub_drain(h);
  CHECK_EQ(v.size(), 1); if (v.size() != 1) return; CHECK_EQ(v[0].type, HUB_MSG_HUB_BOOT);
}

PPE_TEST(one_message_per_vest_level_change_only, "TC-28") {
  HubPipeline h; FakeHubStore st; hub_boot(h, st, 0);
  uint32_t c[6] = {0, 0, 0, 0, 0, 0}; unsigned total = 0;
  for (uint8_t k = 1; k <= 5; k++) hub_send(h, k, PPE_WORN, ++c[k], 10);
  total += count_type(hub_drain(h), HUB_MSG_COMPLIANCE); CHECK_EQ(total, 1);                       // -> COMPLIANT
  hub_send(h, 2, PPE_NOT_WORN, ++c[2], 20);
  std::vector<HubMsg> v = hub_drain(h); CHECK_EQ(v.size(), 1); if (v.size() != 1) return; CHECK_EQ(v[0].compliant, 0); CHECK_EQ(v[0].nodes_not_ok, (1u << 2));   // -> NON_COMPLIANT
  hub_send(h, 4, PPE_NOT_WORN, ++c[4], 30);  CHECK_EQ(h.messages_pending(), 0);                    // second offender: no change
  hub_send(h, 3, PPE_WORN, ++c[3], 35);      CHECK_EQ(h.messages_pending(), 0);                    // heartbeat, no change
  hub_send(h, 2, PPE_WORN, ++c[2], 40);      CHECK_EQ(h.messages_pending(), 0);                    // node 4 still offends
  CHECK(!h.vest_compliant());
  hub_send(h, 4, PPE_WORN, ++c[4], 50);                                                            // -> COMPLIANT again
  v = hub_drain(h); CHECK_EQ(v.size(), 1); if (v.size() != 1) return; CHECK_EQ(v[0].compliant, 1); CHECK_EQ(v[0].nodes_not_ok, 0);
  uint8_t adv[21]; hub_adv(4, PPE_WORN, c[4], adv);
  for (int i = 0; i < 4; i++) CHECK_EQ(h.process(adv, 21, true, 60 + i).verdict, HUB_V_DUPLICATE);   // 4 copies: no message
  CHECK_EQ(h.messages_pending(), 0);
}

PPE_TEST(nodes_not_ok_lists_every_node_that_is_not_worn, "TC-28") {
  HubPipeline h; FakeHubStore st; hub_boot(h, st, 0);
  for (uint8_t k = 1; k <= 5; k++) hub_send(h, k, PPE_WORN, 1, 0);
  hub_drain(h);
  hub_send(h, 1, PPE_NOT_WORN, 2, 1000);                       // message: nodes_not_ok = {1}
  hub_drain(h);
  CHECK_EQ(h.nodes_not_ok_mask(), (1u << 1));
  hub_send(h, 5, PPE_NOT_WORN, 2, 2000);
  CHECK_EQ(h.nodes_not_ok_mask(), (1u << 1) | (1u << 5));
  CHECK_EQ(h.messages_pending(), 0);
}

PPE_TEST(alert_decision_not_worn_or_offline_never_unknown, "TC-28") {
  HubPipeline h; FakeHubStore st; hub_boot(h, st, 0);
  CHECK(!h.alert_active());                                    // all UNKNOWN after boot: no alert
  hub_send(h, 1, PPE_WORN, 1, 10);   CHECK(!h.alert_active()); // WORN with four UNKNOWN: no alert
  hub_send(h, 2, PPE_NOT_WORN, 1, 20); CHECK(h.alert_active());
  hub_send(h, 2, PPE_WORN, 2, 30);   CHECK(!h.alert_active()); // last offender recovers: alert off
  h.tick(10 + 780001u);                                        // node 1 silent since 10 ms: OFFLINE, others UNKNOWN or WORN
  CHECK(h.alert_active());                                     // OFFLINE alerts (D-10)
}

PPE_TEST(message_queue_drops_oldest_when_full_and_counts_it, "TC-28") {
  HubPipeline h; FakeHubStore st; hub_boot(h, st, 0);
  for (uint32_t i = 0; i < HUB_MSG_QUEUE_DEPTH + 4; i++) h.note_fault(HUB_FAULT_NVS_WRITE, 1000 + i);
  CHECK_EQ(h.messages_pending(), HUB_MSG_QUEUE_DEPTH); CHECK_EQ(h.messages_dropped(), 4);
  std::vector<HubMsg> v = hub_drain(h);
  CHECK_EQ(v.size(), HUB_MSG_QUEUE_DEPTH); if (v.size() != HUB_MSG_QUEUE_DEPTH) return;
  CHECK_EQ(v[0].t_ms, 1004);                                   // the four oldest are gone, order kept
  CHECK_EQ(v[v.size() - 1].t_ms, 1000 + HUB_MSG_QUEUE_DEPTH + 3);
}
