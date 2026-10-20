// S3 story 1: hub pipeline module (registry, counters, rate limit). Expected values come from the spec and
// tools/gen_vectors.py output (gen/vectors.h), never from the code under test.
#include "ppe_test.h"
#include "hub_pipeline.h"
#include "fake_hub_store.h"
#include "gen/vectors.h"

static void make_hub(HubPipeline& h, FakeHubStore& st, uint32_t boot_ms = 0) {
  CHECK(h.init(1, V_KEY));
  CHECK(h.load_registry(&st, boot_ms));
}

PPE_TEST(startup_refuses_invalid_vest_ids, "TC-25") {
  CHECK_EQ(hub_startup_check(0x00, true), HUB_START_BAD_VEST);
  CHECK_EQ(hub_startup_check(0xFF, true), HUB_START_BAD_VEST);
  CHECK_EQ(hub_startup_check(0x01, true), HUB_START_OK);
  CHECK_EQ(hub_startup_check(0xFE, true), HUB_START_OK);
  CHECK_EQ(hub_startup_check(0x01, false), HUB_START_NO_KEY);
  HubPipeline h;
  CHECK(!h.init(0x00, V_KEY));
  CHECK(!h.init(0xFF, V_KEY));
  CHECK(!h.init(0x01, nullptr));
  FakeHubStore st;
  h.load_registry(&st, 0);
  CHECK(!h.accept_enabled());                       // never enabled without valid provisioning
}

PPE_TEST(a1_accepted_updates_registry_and_counters, "TC-23") {
  HubPipeline h; FakeHubStore st; make_hub(h, st, 100);
  HubResult r = h.process(V_A1_ADV, 21, true, 5000);
  CHECK_EQ(r.verdict, HUB_V_ACCEPT);
  CHECK_EQ(r.node_id, 3); CHECK_EQ(r.counter, 258); CHECK_EQ(r.missed, 257);   // L = 0 -> c - L - 1
  const HubNode& n = h.node(3);
  CHECK_EQ(n.last_counter, 258); CHECK_EQ(n.last_seen_ms, 5000);
  CHECK_EQ(n.state, HUB_NODE_WORN); CHECK_EQ(n.battery, 90);
  CHECK_EQ(n.c.accepted, 1); CHECK_EQ(n.c.gap_count, 257);
  CHECK_EQ(h.global().rx_total, 1);
  CHECK_EQ(h.node(1).state, HUB_NODE_UNKNOWN);      // other slots untouched
  CHECK_EQ(h.node(1).last_seen_ms, 100);            // boot time until a packet arrives
}

PPE_TEST(five_copies_one_accept_four_duplicates_last_seen_once, "TC-24") {
  HubPipeline h; FakeHubStore st; make_hub(h, st);
  CHECK_EQ(h.process(V_A1_ADV, 21, true, 1000).verdict, HUB_V_ACCEPT);
  for (int i = 1; i <= 4; i++)
    CHECK_EQ(h.process(V_A1_ADV, 21, true, 1000 + 500 * i).verdict, HUB_V_DUPLICATE);
  const HubNode& n = h.node(3);
  CHECK_EQ(n.c.accepted, 1); CHECK_EQ(n.c.dup_count, 4); CHECK_EQ(n.c.replay_count, 0);
  CHECK_EQ(n.last_seen_ms, 1000);                   // duplicates never change last_seen (spec 6.2, P-11)
  CHECK_EQ(h.global().rx_total, 5);
}

PPE_TEST(replay_anomaly_reported_once_per_node_per_60s, "TC-24") {
  HubPipeline h; FakeHubStore st; make_hub(h, st);
  st.has[3] = true; st.val[3] = 1000;
  CHECK(h.load_registry(&st, 0));
  // A.1 has counter 258 < 1000: replay
  HubResult r = h.process(V_A1_ADV, 21, true, 10);
  CHECK_EQ(r.verdict, HUB_V_REPLAY); CHECK(r.anomaly_report); CHECK_EQ(r.last_counter, 1000);
  r = h.process(V_A1_ADV, 21, true, 59999 + 10);          // 59 999 ms later: limited
  CHECK_EQ(r.verdict, HUB_V_REPLAY); CHECK(!r.anomaly_report);
  r = h.process(V_A1_ADV, 21, true, 60000 + 10);          // exactly 60 s after the report: reported again
  CHECK_EQ(r.verdict, HUB_V_REPLAY); CHECK(r.anomaly_report);
  CHECK_EQ(h.node(3).c.replay_count, 3);                  // counted every time, reported rate limited
  CHECK_EQ(h.node(3).last_counter, 1000);                 // a replay never lowers last_counter
  CHECK_EQ(h.node(3).c.accepted, 0);
}

PPE_TEST(anomaly_rate_limit_survives_32bit_ms_wrap, "TC-24") {
  HubPipeline h; FakeHubStore st; make_hub(h, st);
  st.has[3] = true; st.val[3] = 1000; h.load_registry(&st, 0);
  uint32_t t0 = 0xFFFFFF00u;
  CHECK(h.process(V_A1_ADV, 21, true, t0).anomaly_report);
  CHECK(!h.process(V_A1_ADV, 21, true, t0 + 30000u).anomaly_report);   // wrapped past 2^32
  CHECK(h.process(V_A1_ADV, 21, true, t0 + 60000u).anomaly_report);
}

PPE_TEST(anomaly_rate_limit_is_per_node, "TC-24") {
  HubPipeline h; FakeHubStore st; make_hub(h, st);
  st.has[3] = true; st.val[3] = 1000; st.has[5] = true; st.val[5] = 1000; h.load_registry(&st, 0);
  uint8_t a2[21] = {0x14, 0xFF, 0xFF, 0xFF};            // Appendix A.2: node 5, counter 259, state 0
  memcpy(a2 + 4, V_A2_PAYLOAD, 17);
  CHECK(h.process(V_A1_ADV, 21, true, 10).anomaly_report);   // node 3 reported
  CHECK(h.process(a2, 21, true, 20).anomaly_report);          // node 5 has its own limit, also reported
  CHECK(!h.process(V_A1_ADV, 21, true, 30).anomaly_report);   // node 3 limited
  CHECK(!h.process(a2, 21, true, 40).anomaly_report);         // node 5 limited
  CHECK_EQ(h.node(3).c.replay_count, 2); CHECK_EQ(h.node(5).c.replay_count, 2);
}

PPE_TEST(unreadable_store_disables_accept_failsafe, "TC-25") {
  HubPipeline h; FakeHubStore st; st.read_error = true;
  CHECK(h.init(1, V_KEY));
  CHECK(!h.load_registry(&st, 0));
  CHECK(!h.accept_enabled());
  HubResult r = h.process(V_A1_ADV, 21, true, 10);
  CHECK_EQ(r.verdict, HUB_V_DROP_DISABLED);
  CHECK_EQ(h.global().rx_total, 1);
  CHECK_EQ(h.node(3).c.accepted, 0);
  CHECK(!h.load_registry(nullptr, 0));                    // no store at all is also unreadable
}

PPE_TEST(registry_loaded_from_store_states_unknown, "TC-25") {
  HubPipeline h; FakeHubStore st;
  st.has[1] = true; st.val[1] = 77; st.has[4] = true; st.val[4] = 4000000000u;
  CHECK(h.init(1, V_KEY)); CHECK(h.load_registry(&st, 42));
  CHECK_EQ(h.node(1).last_counter, 77); CHECK_EQ(h.node(4).last_counter, 4000000000u);
  CHECK_EQ(h.node(2).last_counter, 0);                    // missing key = fresh node, L = 0
  for (uint8_t i = 1; i <= 5; i++) { CHECK_EQ(h.node(i).state, HUB_NODE_UNKNOWN); CHECK_EQ(h.node(i).last_seen_ms, 42); }
  CHECK_EQ(h.global().rx_total, 0);
  CHECK_EQ(h.node(0).last_counter, 0); CHECK_EQ(h.node(6).last_counter, 0);   // out-of-range ids are harmless
}
