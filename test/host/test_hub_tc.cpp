// S3 story 7: host unit tests of the hub pipeline function, mapped to the bench tests TC-22, 23, 24, 25, 32, 72.
// Bench runs on the hardware repeat these through the real BLE path; here the expectations come from
// tools/gen_vectors.py (gen/vectors.h) and from the spec text, never from the code under test.
#include "ppe_test.h"
#include <map>
#include <time.h>
#include "ppe_proto.h"
#include "ppe_crypto.h"
#include "hub_pipeline.h"
#include "hub_persist.h"
#include "fake_hub_store.h"
#include "gen/vectors.h"

static void fresh(HubPipeline& h, FakeHubStore& st) { CHECK(h.init(1, V_KEY)); CHECK(h.load_registry(&st, 0)); }

// vectors.h result codes: 0 OK, 1 DROP_LEN, 2 DROP_VER, 3 DROP_VEST, 4 DROP_NODE, 5 HMAC_FAIL, 6 DROP_STATE
struct Tally { unsigned rx, len, ver, vest, node, state; unsigned hmac[8], acc[8], dup[8], rep[8]; };
static Tally tally_of(const HubPipeline& h) {
  Tally t; memset(&t, 0, sizeof t);
  const HubGlobalCounters& g = h.global();
  t.rx = g.rx_total; t.len = g.drop_len; t.ver = g.drop_ver; t.vest = g.drop_vest; t.node = g.drop_node; t.state = g.drop_state;
  for (uint8_t i = 1; i <= 5; i++) { t.hmac[i] = h.node(i).c.hmac_fail; t.acc[i] = h.node(i).c.accepted; t.dup[i] = h.node(i).c.dup_count; t.rep[i] = h.node(i).c.replay_count; }
  return t;
}

PPE_TEST(tc22_each_defect_raises_only_its_own_counter, "TC-22") {
  for (unsigned i = 0; i < V_CASES_N; i++) {
    HubPipeline h; FakeHubStore st; fresh(h, st);
    HubResult r = h.process(V_CASES[i].data, V_CASES[i].len, V_CASES[i].nonconn != 0, 1000);
    Tally t = tally_of(h);
    unsigned others = t.len + t.ver + t.vest + t.node + t.state;
    for (int n = 1; n <= 5; n++) others += t.hmac[n] + t.acc[n] + t.dup[n] + t.rep[n];
    CHECK_EQ(t.rx, 1);
    switch (V_CASES[i].expect) {
      case 0: CHECK_EQ(r.verdict, HUB_V_ACCEPT); CHECK_EQ(t.acc[3], 1); CHECK_EQ(others, 1); break;
      case 1: CHECK_EQ(r.verdict, HUB_V_DROP_LEN); CHECK_EQ(t.len, 1); CHECK_EQ(others, 1); break;
      case 2: CHECK_EQ(r.verdict, HUB_V_DROP_VER); CHECK_EQ(t.ver, 1); CHECK_EQ(others, 1); break;
      case 3: CHECK_EQ(r.verdict, HUB_V_DROP_VEST); CHECK_EQ(t.vest, 1); CHECK_EQ(others, 1); break;
      case 4: CHECK_EQ(r.verdict, HUB_V_DROP_NODE); CHECK_EQ(t.node, 1); CHECK_EQ(others, 1); break;
      case 5: CHECK_EQ(r.verdict, HUB_V_HMAC_FAIL); CHECK_EQ(t.hmac[3], 1); CHECK_EQ(others, 1); break;
      case 6: CHECK_EQ(r.verdict, HUB_V_DROP_STATE); CHECK_EQ(t.state, 1); CHECK_EQ(others, 1); break;
      default: CHECK(false);
    }
    if (V_CASES[i].expect != 0) CHECK_EQ(h.node(3).last_counter, 0);       // a rejected packet never moves last_counter
  }
}

PPE_TEST(tc22_state_02_with_valid_hmac_is_rejected_and_counted_as_drop_state, "TC-22") {
  HubPipeline h; FakeHubStore st; fresh(h, st);
  for (unsigned i = 0; i < V_CASES_N; i++) {
    if (strcmp(V_CASES[i].name, "state02_valid_hmac") != 0) continue;
    HubResult r = h.process(V_CASES[i].data, V_CASES[i].len, true, 1);
    CHECK_EQ(r.verdict, HUB_V_DROP_STATE); CHECK_EQ(r.node_id, 3);
    CHECK_EQ(h.global().drop_state, 1); CHECK_EQ(h.node(3).c.hmac_fail, 0); CHECK_EQ(h.node(3).c.accepted, 0);
    return;
  }
  CHECK(false);   // vector missing
}

PPE_TEST(tc22_not_nonconn_pdu_is_drop_len, "TC-22") {
  HubPipeline h; FakeHubStore st; fresh(h, st);
  CHECK_EQ(h.process(V_A1_ADV, 21, false, 1).verdict, HUB_V_DROP_LEN);     // right bytes, connectable PDU type
  CHECK_EQ(h.global().drop_len, 1);
  CHECK_EQ(h.process(nullptr, 21, true, 2).verdict, HUB_V_DROP_LEN);       // null pointer must not crash
  CHECK_EQ(h.process(V_A1_ADV, 0, true, 3).verdict, HUB_V_DROP_LEN);
}

PPE_TEST(tc23_a1_accepted_then_136_flips_rejected_last_counter_unchanged, "TC-23") {
  HubPipeline h; FakeHubStore st; fresh(h, st);
  CHECK_EQ(h.process(V_A1_ADV, 21, true, 1).verdict, HUB_V_ACCEPT);
  CHECK_EQ(h.node(3).last_counter, 258);
  unsigned rejected = 0;
  for (int i = 0; i < 136; i++) {
    uint8_t adv[21] = {0x14, 0xFF, 0xFF, 0xFF}; memcpy(adv + 4, V_FLIPS[i], 17);
    HubResult r = h.process(adv, 21, true, 10 + (uint32_t)i);
    CHECK(r.verdict != HUB_V_ACCEPT && r.verdict != HUB_V_DUPLICATE && r.verdict != HUB_V_REPLAY);
    static const HubVerdict by_stage[7] = {HUB_V_ACCEPT, HUB_V_DROP_LEN, HUB_V_DROP_VER, HUB_V_DROP_VEST, HUB_V_DROP_NODE, HUB_V_HMAC_FAIL, HUB_V_DROP_STATE};
    CHECK_EQ(r.verdict, by_stage[V_FLIP_EXPECT[i]]);
    rejected++;
  }
  CHECK_EQ(rejected, 136);
  CHECK_EQ(h.node(3).last_counter, 258); CHECK_EQ(h.node(3).c.accepted, 1);
  CHECK_EQ(h.global().rx_total, 137);
  Tally t = tally_of(h);
  CHECK_EQ(t.acc[1] + t.acc[2] + t.acc[4] + t.acc[5], 0);                  // no flip was accepted on any node, whichever id it hit
}

PPE_TEST(tc24_five_copies_replays_and_reset_with_persistence, "TC-24") {
  FakeHubStore st; HubPersist ps;
  HubPipeline h; fresh(h, st);
  // 5 copies of one transmission = 1 accepted + 4 duplicates
  for (int c = 0; c < 5; c++) {
    HubResult r = h.process(V_A1_ADV, 21, true, 1000 + 500u * c);
    if (c == 0) { CHECK_EQ(r.verdict, HUB_V_ACCEPT); ps.note(r.node_id, r.counter); }
    else CHECK_EQ(r.verdict, HUB_V_DUPLICATE);
  }
  CHECK_EQ(h.node(3).c.accepted, 1); CHECK_EQ(h.node(3).c.dup_count, 4); CHECK_EQ(h.node(3).last_seen_ms, 1000);
  ps.drain(&st);                                                           // the writer task ran (wait >= 2 s on the bench)
  // replays: counters below L are rejected, anomaly reported, last_counter stays
  uint8_t lo[21]; memcpy(lo, V_A1_ADV, 21);                                // A.1 with L raised above 258 by a newer packet
  st.has[3] = true; st.val[3] = 5000;
  HubPipeline h2; fresh(h2, st);
  CHECK_EQ(h2.process(V_A1_ADV, 21, true, 7).verdict, HUB_V_REPLAY);
  CHECK_EQ(h2.node(3).last_counter, 5000); CHECK_EQ(h2.node(3).c.replay_count, 1);
  // the last accepted packet replayed after a hub reset is rejected (c == L -> duplicate, never accepted)
  HubPipeline h3; FakeHubStore st3; st3.has[3] = true; st3.val[3] = 258; fresh(h3, st3);
  CHECK_EQ(h3.process(V_A1_ADV, 21, true, 9).verdict, HUB_V_DUPLICATE);
  CHECK_EQ(h3.node(3).c.accepted, 0);
}

PPE_TEST(tc25_reset_retains_last_counter_and_resets_states_and_counters, "TC-25") {
  FakeHubStore st; HubPersist ps;
  { HubPipeline h; fresh(h, st);
    HubResult r = h.process(V_A1_ADV, 21, true, 100); ps.note(r.node_id, r.counter); ps.drain(&st);
    CHECK_EQ(h.node(3).state, HUB_NODE_WORN); CHECK_EQ(h.global().rx_total, 1); }
  HubPipeline h; fresh(h, st);                                             // "reset"
  CHECK_EQ(h.node(3).last_counter, 258);
  for (uint8_t i = 1; i <= 5; i++) CHECK_EQ(h.node(i).state, HUB_NODE_UNKNOWN);   // unknown = non-compliant (fail safe)
  CHECK_EQ(h.global().rx_total, 0); CHECK_EQ(h.node(3).c.accepted, 0);
}

// TC-32: a mixed stream, counters must equal an independently kept tally.
PPE_TEST(tc32_counters_equal_the_numbers_injected, "TC-32") {
  HubPipeline h; FakeHubStore st; fresh(h, st);
  Tally want; memset(&want, 0, sizeof want);
  uint32_t last[8] = {0};
  uint32_t now = 1;
  // 1) the defect set, in order, on a hub that has nothing accepted yet
  for (unsigned i = 0; i < V_CASES_N; i++) {
    const VCase& c = V_CASES[i];
    h.process(c.data, c.len, c.nonconn != 0, now++); want.rx++;
    switch (c.expect) {
      case 1: want.len++; break; case 2: want.ver++; break; case 3: want.vest++; break; case 4: want.node++; break;
      case 5: want.hmac[c.data[6]]++; break; case 6: want.state++; break;
      case 0: { uint32_t ctr = (uint32_t)c.data[9] | ((uint32_t)c.data[10] << 8); uint8_t n = c.data[6];
                if (ctr > last[n]) { last[n] = ctr; want.acc[n]++; } else if (ctr == last[n]) want.dup[n]++; else want.rep[n]++; } break;
    }
  }
  // 2) 136 flips and 1000 forged
  for (int i = 0; i < 136; i++) {
    uint8_t adv[21] = {0x14, 0xFF, 0xFF, 0xFF}; memcpy(adv + 4, V_FLIPS[i], 17);
    h.process(adv, 21, true, now++); want.rx++;
    switch (V_FLIP_EXPECT[i]) { case 2: want.ver++; break; case 3: want.vest++; break; case 4: want.node++; break;
      case 5: want.hmac[adv[6]]++; break; case 6: want.state++; break; default: CHECK(false); }
  }
  for (unsigned i = 0; i < V_FORGED_N; i++) {
    uint8_t adv[21] = {0x14, 0xFF, 0xFF, 0xFF}; memcpy(adv + 4, V_FORGED[i], 17);
    h.process(adv, 21, true, now++); want.rx++; want.hmac[adv[6]]++;
  }
  // 3) 7 extra copies of A.1 (now duplicates of the accepted one)
  for (int i = 0; i < 7; i++) { h.process(V_A1_ADV, 21, true, now++); want.rx++; want.dup[3]++; }
  Tally got = tally_of(h);
  CHECK_EQ(got.rx, want.rx); CHECK_EQ(got.len, want.len); CHECK_EQ(got.ver, want.ver); CHECK_EQ(got.vest, want.vest);
  CHECK_EQ(got.node, want.node); CHECK_EQ(got.state, want.state);
  for (int n = 1; n <= 5; n++) { CHECK_EQ(got.hmac[n], want.hmac[n]); CHECK_EQ(got.acc[n], want.acc[n]); CHECK_EQ(got.dup[n], want.dup[n]); CHECK_EQ(got.rep[n], want.rep[n]); }
  unsigned sum = got.len + got.ver + got.vest + got.node + got.state;       // rx_total identity: every packet lands in exactly one bucket
  for (int n = 1; n <= 5; n++) sum += got.hmac[n] + got.acc[n] + got.dup[n] + got.rep[n];
  CHECK_EQ(sum, got.rx);
}

static void make_adv(uint8_t node, uint32_t ctr, uint8_t out[21]) {
  ppe_packet_t pk; memset(&pk, 0, sizeof pk);
  pk.flags = ppe_flags_make(PPE_HEARTBEAT, false, false, false); pk.vest_id = 1; pk.node_id = node; pk.state = PPE_WORN; pk.battery = 90; pk.counter = ctr;
  uint8_t payload[PPE_PAYLOAD_LEN]; ppe_encode_payload(pk, payload);
  CHECK(ppe_sign_payload(V_KEY, payload)); ppe_make_adv(payload, out);
}

PPE_TEST(tc72_thousand_packets_and_a_burst_of_25_copies_all_processed, "TC-72") {
  HubPipeline h; FakeHubStore st; fresh(h, st);
  uint8_t adv[21]; clock_t t0 = clock();
  for (uint32_t i = 1; i <= 1000; i++) { make_adv((uint8_t)(1 + i % 5), i, adv); CHECK_EQ(h.process(adv, 21, true, i).verdict, HUB_V_ACCEPT); }
  double host_us = 1e6 * (double)(clock() - t0) / CLOCKS_PER_SEC / 1000.0;
  fprintf(stderr, "  info: host build+process+HMAC %.1f us per packet (hardware number comes from the bench, TC-72)\n", host_us);
  CHECK_EQ(h.global().rx_total, 1000);
  // burst: 5 nodes x 5 copies back to back = 25 advertisements, every one processed
  HubPipeline b; FakeHubStore sb; fresh(b, sb);
  for (uint8_t n = 1; n <= 5; n++) { make_adv(n, 100 + n, adv); for (int c = 0; c < 5; c++) b.process(adv, 21, true, 50u * c); }
  CHECK_EQ(b.global().rx_total, 25);
  for (uint8_t n = 1; n <= 5; n++) { CHECK_EQ(b.node(n).c.accepted, 1); CHECK_EQ(b.node(n).c.dup_count, 4); }
}

PPE_TEST(tc72_persist_is_never_called_from_the_receive_function, "TC-72") {
  FakeHubStore st; HubPipeline h; fresh(h, st);
  uint8_t adv[21];
  for (uint32_t i = 1; i <= 50; i++) { make_adv(2, i, adv); h.process(adv, 21, true, i); }
  CHECK_EQ(st.writes, 0);                                                  // process() never touches the store (no NVS in the BLE path)
}
