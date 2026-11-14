// S5 TC-27 (offline detection) and TC-31 (hub boot), HOST PART ONLY. Both test cases are level S: the bench run on the
// hub with the injector is what passes them. These tests prove the logic with a simulated clock (a 780 s timeout runs
// in milliseconds) and the set margin / set heartbeat hook parser. Expected values come from FR-27, FR-31, FR-41 and
// protocol spec 1.1 / 8.2, never from the code under test.
#include "ppe_test.h"
#include "hub_test_util.h"
#include "hub_hooks.h"

// First tick time (ms after t0) at which `node` is OFFLINE, ticking once per second with a phase offset, or 0 if never
// within limit_ms. The last accepted packet was at t0.
static uint32_t first_offline_after(HubPipeline& h, uint8_t node, uint32_t t0, uint32_t phase_ms, uint32_t limit_ms) {
  for (uint32_t t = phase_ms; t <= limit_ms; t += 1000u) {
    h.tick(t0 + t);
    if (h.node(node).state == HUB_NODE_OFFLINE) return t;
  }
  return 0;
}

PPE_TEST(offline_between_780_and_785_s_after_the_last_packet_default_margin, "TC-27") {
  const uint32_t phases[] = {0, 1, 250, 500, 999};       // the 1 s tick has an unknown phase against the last packet
  for (unsigned i = 0; i < 5; i++) {
    HubPipeline h; FakeHubStore st; hub_boot(h, st, 0);
    CHECK_EQ(h.offline_timeout_s(), 780);
    hub_send(h, 2, PPE_WORN, 1, 100000);
    uint32_t t = first_offline_after(h, 2, 100000, phases[i], 900000);
    CHECK(t >= 780000u && t <= 785000u);                 // FR-27 acceptance window
    CHECK(t > 780000u);                                  // spec 8.2 "more than": exactly 780.000 s is not yet OFFLINE
  }
}

PPE_TEST(offline_between_720_and_725_s_with_margin_zero, "TC-27") {
  for (uint32_t phase = 0; phase < 1000; phase += 333) {
    HubPipeline h; FakeHubStore st; hub_boot(h, st, 0);
    CHECK(h.set_offline_timing(360, 0));
    CHECK_EQ(h.offline_timeout_s(), 720);
    hub_send(h, 4, PPE_NOT_WORN, 1, 50000);
    uint32_t t = first_offline_after(h, 4, 50000, phase, 900000);
    CHECK(t >= 720000u && t <= 725000u);
  }
}

PPE_TEST(timeout_follows_heartbeat_and_margin_and_bounds_are_enforced, "TC-27") {
  HubPipeline h;
  CHECK_EQ(h.heartbeat_s(), 360); CHECK_EQ(h.margin_s(), 60); CHECK_EQ(h.offline_timeout_s(), 780);   // defaults (spec 1.1)
  CHECK(h.set_offline_timing(60, 0));    CHECK_EQ(h.offline_timeout_s(), 120);     // FR-41: a hub set to 60 s declares OFFLINE at 120 s plus margin
  CHECK(h.set_offline_timing(60, 30));   CHECK_EQ(h.offline_timeout_s(), 150);
  CHECK(h.set_offline_timing(3600, 300)); CHECK_EQ(h.offline_timeout_s(), 7500);
  CHECK(h.set_offline_timing(360, 60));  CHECK_EQ(h.offline_timeout_s(), 780);
  CHECK(!h.set_offline_timing(59, 60));   CHECK(!h.set_offline_timing(3601, 60)); CHECK(!h.set_offline_timing(360, 301));
  CHECK_EQ(h.heartbeat_s(), 360); CHECK_EQ(h.margin_s(), 60);                        // a refused value changes nothing
}

PPE_TEST(one_dropped_heartbeat_does_not_cause_offline, "TC-27") {
  // Heartbeats every 360 s, one lost with all 5 copies: the gap is at most 2 x 378 s (FR-09 upper bound) = 756 s < 780 s.
  HubPipeline h; FakeHubStore st; hub_boot(h, st, 0);
  hub_send(h, 3, PPE_WORN, 1, 0);
  hub_run_ticks(h, 1000, 756000);
  CHECK_EQ(h.node(3).state, HUB_NODE_WORN);
  hub_send(h, 3, PPE_WORN, 2, 756000);                  // the next heartbeat after the lost one
  hub_run_ticks(h, 757000, 756000 + 756000);
  CHECK_EQ(h.node(3).state, HUB_NODE_WORN);
  // nominal 360 s heartbeats for two simulated hours never go OFFLINE
  HubPipeline g; FakeHubStore sg; hub_boot(g, sg, 0);
  uint32_t ctr = 0;
  for (uint32_t t = 0; t <= 7200000u; t += 1000u) { if (t % 360000u == 0) hub_send(g, 5, PPE_WORN, ++ctr, t); g.tick(t); CHECK_EQ(g.node(5).state, HUB_NODE_WORN); }
}

PPE_TEST(margin_zero_with_a_lost_heartbeat_goes_offline_why_the_margin_is_at_least_40_s, "TC-27") {
  // FR-41 recommends a margin of at least 40 s: with margin 0 the allowed 756 s gap exceeds the 720 s timeout.
  HubPipeline h; FakeHubStore st; hub_boot(h, st, 0);
  CHECK(h.set_offline_timing(360, 0));
  hub_send(h, 3, PPE_WORN, 1, 0);
  hub_run_ticks(h, 1000, 756000);
  CHECK_EQ(h.node(3).state, HUB_NODE_OFFLINE);
  CHECK(h.set_offline_timing(360, 40));                 // recommended minimum margin: 760 s timeout covers 756 s
  HubPipeline g; FakeHubStore sg; hub_boot(g, sg, 0); CHECK(g.set_offline_timing(360, 40));
  hub_send(g, 3, PPE_WORN, 1, 0);
  hub_run_ticks(g, 1000, 756000);
  CHECK_EQ(g.node(3).state, HUB_NODE_WORN);
}

PPE_TEST(a_tick_clock_slightly_behind_last_seen_never_gives_a_false_offline, "TC-27") {
  // Race on the hub: the tick task reads its clock, the rx task then stamps last_seen a few ms later, then the tick runs.
  HubPipeline h; FakeHubStore st; hub_boot(h, st, 0);
  hub_send(h, 3, PPE_WORN, 1, 1000000);
  for (uint32_t behind = 1; behind <= 50; behind++) { h.tick(1000000 - behind); CHECK_EQ(h.node(3).state, HUB_NODE_WORN); }
  std::vector<HubMsg> v = hub_drain(h);                  // nodes 1, 2, 4, 5 were never heard (UNKNOWN since boot): they do time out
  for (size_t i = 0; i < v.size(); i++) CHECK(!(v[i].type == HUB_MSG_NODE_OFFLINE && v[i].node_id == 3));
}

PPE_TEST(offline_detection_survives_the_32_bit_millisecond_wrap, "TC-27") {
  HubPipeline h; FakeHubStore st; hub_boot(h, st, 0xFFFFFFFFu - 100000u);
  uint32_t t0 = 0xFFFFFFFFu - 50000u;                    // last packet 50 s before the wrap
  hub_send(h, 1, PPE_WORN, 1, t0);
  uint32_t t = first_offline_after(h, 1, t0, 0, 900000);
  CHECK(t > 780000u && t <= 781000u);
}

PPE_TEST(set_margin_and_set_heartbeat_hook_parser, "TC-27") {
  HubHookCmd c = hub_hook_parse("set margin 0");        CHECK_EQ(c.type, HUB_HOOK_SET_MARGIN);    CHECK_EQ(c.val, 0);
  c = hub_hook_parse("  set   heartbeat   60  \r\n");  CHECK_EQ(c.type, HUB_HOOK_SET_HEARTBEAT); CHECK_EQ(c.val, 60);
  c = hub_hook_parse("set margin 300");                 CHECK_EQ(c.type, HUB_HOOK_SET_MARGIN);    CHECK_EQ(c.val, 300);
  c = hub_hook_parse("set margin 301");                 CHECK_EQ(c.type, HUB_HOOK_SET_MARGIN);    CHECK_EQ(c.val, 301);   // bounds belong to the pipeline
  const char* bad[] = {"set", "set margin", "set margin x", "set margin -1", "set margin 1 2", "set margin 1234567", "set foo 1",
                       "set marginx 1", "set heartbeatx 60", "setmargin 5", "set margin 0x10", "set margin 5s"};
  for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) CHECK_EQ(hub_hook_parse(bad[i]).type, HUB_HOOK_ERROR);
  CHECK_EQ(hub_hook_parse("stall 1").type, HUB_HOOK_STALL);   // the old commands still parse
  CHECK_EQ(hub_hook_parse("dump").type, HUB_HOOK_DUMP);
}

// ---------------- TC-31 host part ----------------
PPE_TEST(boot_states_unknown_no_alert_hub_boot_first, "TC-31") {
  FakeHubStore st; st.has[2] = true; st.val[2] = 500;   // a retained counter does not make the node known
  HubPipeline h; CHECK(h.init(1, V_KEY)); CHECK(h.load_registry(&st, 12345));
  h.note_boot(3, 12345);
  for (uint8_t n = 1; n <= 5; n++) CHECK_EQ(h.node(n).state, HUB_NODE_UNKNOWN);
  CHECK(!h.alert_active()); CHECK(!h.vest_compliant());
  std::vector<HubMsg> v = hub_drain(h);
  CHECK_EQ(v.size(), 1); if (v.size() != 1) return; CHECK_EQ(v[0].type, HUB_MSG_HUB_BOOT); CHECK_EQ(v[0].reset_reason, 3); CHECK_EQ(v[0].vest_id, 1);   // HUB_BOOT first, nothing else
}

PPE_TEST(silent_node_offline_between_780_and_785_s_after_boot_others_stay_online, "TC-31") {
  const uint32_t boot = 777777;
  HubPipeline h; FakeHubStore st; hub_boot(h, st, boot);
  h.note_boot(1, boot); hub_drain(h);
  uint32_t ctr = 0; uint32_t off_at = 0;
  for (uint32_t t = 1000; t <= 900000u; t += 1000u) {
    if (t % 360000u == 0 || t == 5000u) for (uint8_t n = 1; n <= 4; n++) hub_send(h, n, PPE_WORN, ++ctr, boot + t);   // nodes 1-4 run, node 5 is silent
    h.tick(boot + t);
    if (!off_at && h.node(5).state == HUB_NODE_OFFLINE) off_at = t;
  }
  CHECK(off_at >= 780000u && off_at <= 785000u);
  for (uint8_t n = 1; n <= 4; n++) CHECK_EQ(h.node(n).state, HUB_NODE_WORN);
  CHECK(h.alert_active());                               // OFFLINE starts the alert (D-10)
  std::vector<HubMsg> v = hub_drain(h);
  unsigned off5 = 0; for (size_t i = 0; i < v.size(); i++) if (v[i].type == HUB_MSG_NODE_OFFLINE && v[i].node_id == 5) { off5++; CHECK(v[i].last_seen_age_s >= 780 && v[i].last_seen_age_s <= 781); }
  CHECK_EQ(off5, 1);
}

PPE_TEST(no_alert_while_every_node_is_unknown_even_after_780_s_with_accept_disabled_it_goes_offline, "TC-31") {
  // Registry unreadable (B-04b): accept disabled, the nodes still time out from boot and the alert runs.
  FakeHubStore st; st.read_error = true;
  HubPipeline h; CHECK(h.init(1, V_KEY)); CHECK(!h.load_registry(&st, 0));
  hub_run_ticks(h, 1000, 779000);
  CHECK(!h.alert_active());                              // 779 s: still UNKNOWN, no alert
  hub_run_ticks(h, 780000, 785000);
  for (uint8_t n = 1; n <= 5; n++) CHECK_EQ(h.node(n).state, HUB_NODE_OFFLINE);
  CHECK(h.alert_active());
}
