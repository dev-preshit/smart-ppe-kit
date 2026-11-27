// S4 host tests: transmit sequencer (state machines 1.7). Injected clock, no radio.
#include "ppe_test.h"
#include "ppe_tx.h"
#include <vector>

namespace {
struct Act { uint32_t t; ppe_tx_act_t a; uint8_t copy; };

ppe_event_t ev(ppe_wear_t s, bool boot = false) { ppe_event_t e; e.pair = ppe_raw_make(s, 0); e.boot = boot; return e; }

// Runs the sequencer one millisecond at a time and records every action. After BUILD it calls begin() like the firmware does.
std::vector<Act> run(ppe_tx_t* t, uint32_t from, uint32_t ms, std::vector<ppe_tx_act_t>* builds = nullptr) {
  std::vector<Act> out;
  for (uint32_t i = 0; i <= ms; i++) {
    uint32_t now = from + i;
    ppe_tx_act_t a = ppe_tx_step(t, now, 100, 500);
    if (a == PPE_TX_ACT_NONE) continue;
    if (a == PPE_TX_ACT_BUILD_EVENT || a == PPE_TX_ACT_BUILD_HEARTBEAT) {
      if (builds) builds->push_back(a);
      ppe_tx_begin(t, now);
    }
    Act x = { now, a, t->copy };
    out.push_back(x);
  }
  return out;
}
}

PPE_TEST(tx_event_sequence_is_five_copies_500_ms_start_to_start_then_silence, "TC-10") {
  ppe_tx_t t; ppe_tx_init(&t);
  CHECK(ppe_tx_request_event(&t, ev(PPE_WEAR_WORN)));
  std::vector<Act> a = run(&t, 1000, 20000);
  std::vector<uint32_t> starts, stops;
  uint32_t t0 = 0;
  for (size_t i = 0; i < a.size(); i++) {
    if (a[i].a == PPE_TX_ACT_BUILD_EVENT) t0 = a[i].t;
    if (a[i].a == PPE_TX_ACT_START_COPY) starts.push_back(a[i].t);
    if (a[i].a == PPE_TX_ACT_STOP_COPY) stops.push_back(a[i].t);
  }
  CHECK_EQ(t0, 1000);
  CHECK_EQ(starts.size(), 4);                 // copy 0 is started by the caller at BUILD, copies 1..4 by START_COPY
  CHECK_EQ(stops.size(), 5);
  for (size_t k = 0; k < starts.size(); k++) CHECK_EQ(starts[k], 1000 + 500 * (k + 1));
  for (size_t k = 0; k < stops.size(); k++) CHECK_EQ(stops[k], 1000 + 500 * k + 100);
  CHECK(!ppe_tx_busy(&t));                    // T-06: idle after copy 4, no sixth copy in the following 10 s and more
  CHECK_EQ(a.back().t, 3100);
}

PPE_TEST(tx_copy_schedule_does_not_drift_when_the_caller_is_late, "TC-10") {
  ppe_tx_t t; ppe_tx_init(&t);
  ppe_tx_request_event(&t, ev(PPE_WEAR_NOT_WORN));
  CHECK_EQ(ppe_tx_step(&t, 5000, 100, 500), PPE_TX_ACT_BUILD_EVENT);
  ppe_tx_begin(&t, 5000);
  CHECK_EQ(ppe_tx_step(&t, 5130, 100, 500), PPE_TX_ACT_STOP_COPY);      // the loop was 30 ms late
  CHECK_EQ(ppe_tx_step(&t, 5517, 100, 500), PPE_TX_ACT_START_COPY);     // 17 ms late
  CHECK_EQ(ppe_tx_step(&t, 5700, 100, 500), PPE_TX_ACT_STOP_COPY);
  CHECK_EQ(ppe_tx_step(&t, 6000, 100, 500), PPE_TX_ACT_START_COPY);     // copy 2 is due at t0 + 1000, not 1017 + 500
  CHECK_EQ(t.copy, 2);
}

PPE_TEST(tx_event_has_priority_and_discards_a_pending_heartbeat, "TC-09") {
  ppe_tx_t t; ppe_tx_init(&t);
  CHECK(ppe_tx_request_heartbeat(&t));
  CHECK(ppe_tx_request_event(&t, ev(PPE_WEAR_WORN, true)));
  std::vector<ppe_tx_act_t> builds;
  run(&t, 0, 20000, &builds);
  CHECK_EQ(builds.size(), 1);
  CHECK_EQ(builds[0], PPE_TX_ACT_BUILD_EVENT);
  CHECK_EQ(t.cur_event.pair.state, PPE_WEAR_WORN);
  CHECK(t.cur_event.boot);
}

PPE_TEST(tx_heartbeat_alone_builds_a_heartbeat_and_a_second_request_is_refused, "TC-09") {
  ppe_tx_t t; ppe_tx_init(&t);
  CHECK(ppe_tx_request_heartbeat(&t));
  CHECK(!ppe_tx_request_heartbeat(&t));       // N-26: no heartbeat already requested
  std::vector<ppe_tx_act_t> builds;
  run(&t, 0, 5000, &builds);
  CHECK_EQ(builds.size(), 1);
  CHECK_EQ(builds[0], PPE_TX_ACT_BUILD_HEARTBEAT);
}

PPE_TEST(tx_sequences_never_interleave_a_heartbeat_requested_during_a_sequence_waits, "TC-10") {
  ppe_tx_t t; ppe_tx_init(&t);
  ppe_tx_request_event(&t, ev(PPE_WEAR_WORN));
  std::vector<ppe_tx_act_t> builds;
  std::vector<Act> a1 = run(&t, 0, 1200, &builds);          // sequence running
  CHECK(ppe_tx_busy(&t));
  CHECK(ppe_tx_request_heartbeat(&t));                      // N-26: sent after it ends
  std::vector<Act> a2 = run(&t, 1201, 20000, &builds);
  CHECK_EQ(builds.size(), 2);
  CHECK_EQ(builds[1], PPE_TX_ACT_BUILD_HEARTBEAT);
  uint32_t hb_t0 = 0;
  for (size_t i = 0; i < a2.size(); i++) if (a2[i].a == PPE_TX_ACT_BUILD_HEARTBEAT) hb_t0 = a2[i].t;
  CHECK(hb_t0 >= 2100);                                     // not before the first sequence ended (t0 = 0, copy 4 ends at 2000 + 100)
  CHECK_EQ(hb_t0, 2101);                                    // and on the very next step after it
}

PPE_TEST(tx_events_requested_during_a_sequence_run_in_order_after_it_and_a_third_is_refused, "TC-07") {
  ppe_tx_t t; ppe_tx_init(&t);
  ppe_tx_request_event(&t, ev(PPE_WEAR_WORN));
  run(&t, 0, 600);
  CHECK(ppe_tx_request_event(&t, ev(PPE_WEAR_NOT_WORN)));
  CHECK(ppe_tx_request_event(&t, ev(PPE_WEAR_WORN)));
  CHECK(!ppe_tx_request_event(&t, ev(PPE_WEAR_NOT_WORN)));   // queue of 2 is full: the caller logs the loss
  std::vector<ppe_wear_t> order;
  for (uint32_t now = 601; now < 20000; now++) {
    ppe_tx_act_t a = ppe_tx_step(&t, now, 100, 500);
    if (a == PPE_TX_ACT_BUILD_EVENT) { order.push_back(t.cur_event.pair.state); ppe_tx_begin(&t, now); }
  }
  CHECK_EQ(order.size(), 2);
  CHECK_EQ(order[0], PPE_WEAR_NOT_WORN);
  CHECK_EQ(order[1], PPE_WEAR_WORN);
}

PPE_TEST(tx_waits_in_build_until_the_caller_begins_and_abort_drops_everything, "TC-17") {
  ppe_tx_t t; ppe_tx_init(&t);
  ppe_tx_request_event(&t, ev(PPE_WEAR_WORN));
  ppe_tx_request_heartbeat(&t);
  CHECK_EQ(ppe_tx_step(&t, 10, 100, 500), PPE_TX_ACT_BUILD_EVENT);
  CHECK_EQ(ppe_tx_step(&t, 11, 100, 500), PPE_TX_ACT_NONE);   // still building: no second BUILD
  CHECK_EQ(ppe_tx_step(&t, 9999, 100, 500), PPE_TX_ACT_NONE);
  ppe_tx_request_event(&t, ev(PPE_WEAR_NOT_WORN));
  ppe_tx_abort(&t);                                           // N-30 / N-31
  CHECK(!ppe_tx_busy(&t));
  CHECK_EQ(ppe_tx_step(&t, 10000, 100, 500), PPE_TX_ACT_NONE);
  ppe_tx_begin(&t, 10000);                                    // begin() outside BUILD does nothing
  CHECK(!ppe_tx_busy(&t));
}

PPE_TEST(tx_timing_is_correct_across_the_32_bit_millisecond_wrap, "TC-10") {
  ppe_tx_t t; ppe_tx_init(&t);
  ppe_tx_request_event(&t, ev(PPE_WEAR_WORN));
  uint32_t start = 0xFFFFFF00u;
  std::vector<Act> a = run(&t, start, 4000);
  int starts = 0, stops = 0;
  for (size_t i = 0; i < a.size(); i++) {
    if (a[i].a == PPE_TX_ACT_START_COPY) { starts++; CHECK_EQ((uint32_t)(a[i].t - start) % 500, 0); }
    if (a[i].a == PPE_TX_ACT_STOP_COPY) stops++;
  }
  CHECK_EQ(starts, 4);
  CHECK_EQ(stops, 5);
}
