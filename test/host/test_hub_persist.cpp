// S3 story 2: persistence of last_counter through the HubStore interface (TC-24 step 4, TC-25).
#include "ppe_test.h"
#include "hub_pipeline.h"
#include "hub_persist.h"
#include "fake_hub_store.h"
#include "gen/vectors.h"

PPE_TEST(persisted_counter_survives_reset_and_blocks_replay, "TC-24") {
  FakeHubStore st; HubPersist ps;
  { HubPipeline h; h.init(1, V_KEY); h.load_registry(&st, 0);
    HubResult r = h.process(V_A1_ADV, 21, true, 1000);
    CHECK_EQ(r.verdict, HUB_V_ACCEPT);
    ps.note(r.node_id, r.counter);
    CHECK_EQ(st.writes, 0);                         // nothing written inside the receive path
    CHECK_EQ(ps.drain(&st), 1);                     // the writer task does it
    CHECK_EQ(st.val[3], 258); }
  HubPipeline h2; h2.init(1, V_KEY); CHECK(h2.load_registry(&st, 0));   // hub reset
  CHECK_EQ(h2.node(3).last_counter, 258); CHECK_EQ(h2.node(3).state, HUB_NODE_UNKNOWN);
  CHECK_EQ(h2.process(V_A1_ADV, 21, true, 2000).verdict, HUB_V_DUPLICATE);   // c == L after the reset
  CHECK_EQ(h2.node(3).c.accepted, 0);
}

PPE_TEST(reset_before_the_write_lands_loses_at_most_the_last_counter, "TC-24") {
  FakeHubStore st; HubPersist ps;
  HubPipeline h; h.init(1, V_KEY); h.load_registry(&st, 0);
  HubResult r = h.process(V_A1_ADV, 21, true, 1000);
  ps.note(r.node_id, r.counter);                    // power cut here: write never ran
  HubPipeline h2; h2.init(1, V_KEY); h2.load_registry(&st, 0);
  CHECK_EQ(h2.node(3).last_counter, 0);             // documented window (spec 6.2): wait >= 2 s before a reset
  CHECK_EQ(ps.pending(), 1);
}

PPE_TEST(coalescing_keeps_newest_per_node_and_never_lowers, "TC-72") {
  FakeHubStore st; HubPersist ps;
  ps.note(2, 10); ps.note(2, 12); ps.note(2, 11); ps.note(4, 7);
  CHECK_EQ(ps.pending(), 2);
  CHECK_EQ(ps.drain(&st), 2);
  CHECK_EQ(st.val[2], 12); CHECK_EQ(st.val[4], 7); CHECK_EQ(st.writes, 2);
  CHECK_EQ(ps.pending(), 0);
}

PPE_TEST(newer_value_during_write_stays_pending, "TC-72") {
  FakeHubStore st; HubPersist ps; uint8_t n; uint32_t c;
  ps.note(1, 5);
  CHECK(ps.next(&n, &c)); CHECK_EQ(n, 1); CHECK_EQ(c, 5);
  ps.note(1, 6);                                    // arrives while the write of 5 runs
  ps.done(n, c, st.write_last_counter(n, c));
  CHECK_EQ(ps.pending(), 1);
  CHECK(ps.next(&n, &c)); CHECK_EQ(c, 6);
}

PPE_TEST(write_failure_is_counted_not_looped_ram_stays_authoritative, "TC-25") {
  FakeHubStore st; HubPersist ps; st.fail_writes = 1;
  HubPipeline h; h.init(1, V_KEY); h.load_registry(&st, 0);
  HubResult r = h.process(V_A1_ADV, 21, true, 1);
  ps.note(r.node_id, r.counter);
  CHECK_EQ(ps.drain(&st), 1);
  CHECK_EQ(ps.writes_failed(), 1); CHECK_EQ(ps.writes_ok(), 0); CHECK_EQ(ps.pending(), 0);
  CHECK_EQ(h.node(3).last_counter, 258);            // RAM copy still protects against replays now
  CHECK_EQ(h.process(V_A1_ADV, 21, true, 2).verdict, HUB_V_DUPLICATE);
}

PPE_TEST(persist_ignores_out_of_range_nodes, "TC-25") {
  HubPersist ps; ps.note(0, 1); ps.note(6, 1); ps.note(255, 1);
  CHECK_EQ(ps.pending(), 0);
}
