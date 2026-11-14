// S5 TC-26: node state machine (FR-26, spec 8.2, state machines H-01..H-13). Host test with a simulated clock.
// Expected values come from the spec 8.2 transition table and from an independent model written in this file,
// never from the code under test.
#include "ppe_test.h"
#include "hub_test_util.h"

static const uint8_t U = HUB_NODE_UNKNOWN, NW = HUB_NODE_NOT_WORN, W = HUB_NODE_WORN, OFF = HUB_NODE_OFFLINE;

// Bring node 3 into `from` and return the next free counter. UNKNOWN: fresh hub. OFFLINE: accept, then let it time out.
static uint32_t reach(HubPipeline& h, uint8_t from, uint32_t* now) {
  uint32_t ctr = 100;
  if (from == U) return ctr;
  if (from == W)  { hub_send(h, 3, PPE_WORN, ctr++, *now); return ctr; }
  if (from == NW) { hub_send(h, 3, PPE_NOT_WORN, ctr++, *now); return ctr; }
  hub_send(h, 3, PPE_WORN, ctr++, *now);                 // OFFLINE
  *now += 781000u; h.tick(*now);
  CHECK_EQ(h.node(3).state, OFF);
  return ctr;
}

PPE_TEST(h01_boot_every_node_unknown_no_alert_no_message, "TC-26") {
  HubPipeline h; FakeHubStore st; hub_boot(h, st, 4242);
  for (uint8_t i = 1; i <= 5; i++) { CHECK_EQ(h.node(i).state, U); CHECK_EQ(h.node(i).last_seen_ms, 4242); }
  CHECK(!h.alert_active()); CHECK(!h.vest_compliant()); CHECK_EQ(h.messages_pending(), 0);
}

PPE_TEST(packet_transitions_h02_h03_h05_h06_h08_h09_h11_h12, "TC-26") {
  struct Row { uint8_t from, pkt_state, to; const char* id; };
  // spec 8.2: an accepted packet sets WORN or NOT_WORN from its state, from any previous state
  const Row rows[] = {
    {U,  PPE_WORN, W, "H-02"},  {U,  PPE_NOT_WORN, NW, "H-03"},
    {W,  PPE_WORN, W, "H-05"},  {W,  PPE_NOT_WORN, NW, "H-06"},
    {NW, PPE_NOT_WORN, NW, "H-08"}, {NW, PPE_WORN, W, "H-09"},
    {OFF, PPE_WORN, W, "H-11"}, {OFF, PPE_NOT_WORN, NW, "H-12"},
  };
  for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++) {
    HubPipeline h; FakeHubStore st; hub_boot(h, st, 0);
    uint32_t now = 1000, ctr = reach(h, rows[i].from, &now);
    hub_drain(h);
    now += 5000;
    HubResult r = hub_send(h, 3, rows[i].pkt_state, ctr, now, 77);
    CHECK_EQ(r.verdict, HUB_V_ACCEPT);                          // row id is rows[i].id, e.g. "H-02"
    CHECK_EQ(r.prev_state, rows[i].from);
    CHECK_EQ(h.node(3).state, rows[i].to);
    CHECK_EQ(h.node(3).last_seen_ms, now);                     // every accepted packet refreshes last_seen
    CHECK_EQ(h.node(3).battery, 77);                           // and battery and flags (H-05, H-08)
    // other slots are untouched by this packet: UNKNOWN, or OFFLINE if the setup waited 781 s for node 3 to time out
    for (uint8_t o = 1; o <= 5; o++) if (o != 3) CHECK_EQ(h.node(o).state, (rows[i].from == OFF ? OFF : U));
  }
}

PPE_TEST(timeouts_h04_h07_h10_each_start_state_goes_offline_once, "TC-26") {
  const uint8_t starts[] = {U, W, NW};
  for (unsigned i = 0; i < 3; i++) {
    HubPipeline h; FakeHubStore st; hub_boot(h, st, 1000);
    uint32_t now = 1000, ctr = reach(h, starts[i], &now); (void)ctr;
    hub_drain(h);
    uint32_t seen = h.node(3).last_seen_ms;
    h.tick(seen + 780000u);                                    // exactly the timeout: not yet
    CHECK_EQ(h.node(3).state, starts[i]);
    h.tick(seen + 780001u);                                    // one ms more: OFFLINE
    CHECK_EQ(h.node(3).state, OFF);
    std::vector<HubMsg> v = hub_drain(h);
    unsigned offline_msgs = 0;
    for (size_t k = 0; k < v.size(); k++)
      if (v[k].type == HUB_MSG_NODE_OFFLINE && v[k].node_id == 3) { offline_msgs++; CHECK_EQ(v[k].last_seen_age_s, 780); }
    CHECK_EQ(offline_msgs, 1);
  }
}

PPE_TEST(h13_offline_node_gives_no_repeated_message, "TC-26") {
  HubPipeline h; FakeHubStore st; hub_boot(h, st, 0);
  hub_send(h, 3, PPE_WORN, 1, 0);
  hub_run_ticks(h, 1000, 3000000);
  std::vector<HubMsg> v = hub_drain(h);
  unsigned off3 = 0; for (size_t k = 0; k < v.size(); k++) if (v[k].type == HUB_MSG_NODE_OFFLINE && v[k].node_id == 3) off3++;
  CHECK_EQ(off3, 1);                                           // 3000 s of ticks, still one message
  CHECK_EQ(h.node(3).state, OFF);
}

PPE_TEST(only_accepted_packets_change_state_or_last_seen, "TC-26") {
  HubPipeline h; FakeHubStore st; hub_boot(h, st, 0);
  hub_send(h, 3, PPE_NOT_WORN, 10, 1000);
  hub_drain(h);
  // duplicate (same counter, even with a different state value: the bytes differ so HMAC would fail; use the same packet)
  CHECK_EQ(hub_send(h, 3, PPE_NOT_WORN, 10, 2000).verdict, HUB_V_DUPLICATE);
  // replay with state WORN: lower counter
  CHECK_EQ(hub_send(h, 3, PPE_WORN, 9, 3000).verdict, HUB_V_REPLAY);
  // forged: bit flip in the tag
  uint8_t adv[21]; hub_adv(3, PPE_WORN, 11, adv); adv[20] ^= 0x01;
  CHECK_EQ(h.process(adv, 21, true, 4000).verdict, HUB_V_HMAC_FAIL);
  // valid HMAC, invalid state value 0x02 (drop_state)
  for (unsigned i = 0; i < V_CASES_N; i++) if (strcmp(V_CASES[i].name, "state02_valid_hmac") == 0) CHECK_EQ(h.process(V_CASES[i].data, V_CASES[i].len, true, 5000).verdict, HUB_V_DROP_STATE);
  CHECK_EQ(h.node(3).state, NW);
  CHECK_EQ(h.node(3).last_seen_ms, 1000);
  CHECK_EQ(h.messages_pending(), 0);
}

PPE_TEST(reload_after_a_run_gives_unknown_again, "TC-26") {
  HubPipeline h; FakeHubStore st; hub_boot(h, st, 0);
  for (uint8_t n = 1; n <= 5; n++) hub_send(h, n, PPE_WORN, 1, 10);
  h.tick(1000000);                                             // all OFFLINE? no: all were refreshed at 10 ms, so yes after 780 s
  CHECK_EQ(h.node(1).state, OFF);
  CHECK(h.load_registry(&st, 2000000));                        // a hub restart
  for (uint8_t n = 1; n <= 5; n++) { CHECK_EQ(h.node(n).state, U); CHECK_EQ(h.node(n).last_seen_ms, 2000000); }
  CHECK_EQ(h.messages_pending(), 0);
}

// Independent model of spec 8.2 and a seeded random walk: packets (state 0/1, counter always rising so every packet is
// accepted), time jumps, 1 s ticks. After every step the pipeline must equal the model, UNKNOWN must never come back
// once left, and an OFFLINE message must be produced exactly when the model goes OFFLINE.
PPE_TEST(random_walk_matches_independent_model_and_never_returns_to_unknown, "TC-26") {
  HubPipeline h; FakeHubStore st; hub_boot(h, st, 5000);
  uint8_t model[6]; uint32_t seen[6]; bool left_unknown[6]; uint32_t ctr[6];
  for (int i = 0; i < 6; i++) { model[i] = U; seen[i] = 5000; left_unknown[i] = false; ctr[i] = 0; }
  uint32_t now = 5000, rng = 12345; unsigned model_offline_events = 0, msg_offline_events = 0;
  for (int step = 0; step < 20000; step++) {
    rng = rng * 1664525u + 1013904223u;
    uint32_t pick = (rng >> 16) % 10;
    if (pick < 3) {                                           // a packet from a random node with a random state
      uint8_t node = (uint8_t)(1 + ((rng >> 8) % 5)), s = (uint8_t)((rng >> 4) & 1);
      now += 1 + (rng >> 20) % 400;
      HubResult r = hub_send(h, node, s ? PPE_WORN : PPE_NOT_WORN, ++ctr[node], now);
      CHECK_EQ(r.verdict, HUB_V_ACCEPT);
      model[node] = s ? W : NW; seen[node] = now;
    } else {                                                  // time passes: 1 s ticks over a random span, up to ~1000 s
      uint32_t span = 1000u * (1 + (rng >> 12) % 1000);
      for (uint32_t t = 1000; t <= span; t += 1000u) {
        now += 1000u; h.tick(now);
        for (uint8_t n = 1; n <= 5; n++)
          if (model[n] != OFF && (uint32_t)(now - seen[n]) > 780000u) { model[n] = OFF; model_offline_events++; }
      }
    }
    std::vector<HubMsg> v = hub_drain(h);
    for (size_t k = 0; k < v.size(); k++) if (v[k].type == HUB_MSG_NODE_OFFLINE) msg_offline_events++;
    for (uint8_t n = 1; n <= 5; n++) {
      CHECK_EQ(h.node(n).state, model[n]);
      if (model[n] != U) left_unknown[n] = true;
      if (left_unknown[n]) CHECK(h.node(n).state != U);
    }
  }
  CHECK_EQ(msg_offline_events, model_offline_events);
  CHECK(model_offline_events > 20);                           // the walk really exercised OFFLINE
}
