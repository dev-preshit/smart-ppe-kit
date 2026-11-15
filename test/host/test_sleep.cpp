// S4 host tests: sleep decisions (state machines N-06, N-07, N-07b, N-10, N-10b; FR-14, FR-15).
#include "ppe_test.h"
#include "ppe_sleep.h"

namespace {
ppe_sleep_in_t in(ppe_node_type_t t, ppe_wear_t conf, bool a, bool b, bool hb_set = true, uint32_t hb_rem = 300000) {
  ppe_sleep_in_t x; x.type = t; x.confirmed = conf; x.contact_a_closed = a; x.contact_b_closed = b;
  x.hb_set = hb_set; x.hb_remaining_ms = hb_rem; x.node_fault = false;
  return x;
}
}

PPE_TEST(sleep_glove_arms_the_gpio_wake_toward_the_opposite_state_and_the_heartbeat_timer, "TC-15") {
  ppe_sleep_state_t s; ppe_sleep_state_init(&s); ppe_sleep_out_t o;
  ppe_sleep_in_t a = in(PPE_NTYPE_GLOVE, PPE_WEAR_NOT_WORN, false, false);          // glove off, contact open: wake when it closes
  ppe_sleep_decide(&s, &a, &o);
  CHECK_EQ(o.mode, PPE_SLEEP_GPIO); CHECK(o.wake_on_closed); CHECK_EQ(o.pin_mask, 1);
  CHECK(o.timer_valid); CHECK_EQ(o.timer_ms, 300000u);
  ppe_sleep_in_t b = in(PPE_NTYPE_GLOVE, PPE_WEAR_WORN, true, false);               // glove on, contact closed: wake when it opens
  ppe_sleep_decide(&s, &b, &o);
  CHECK_EQ(o.mode, PPE_SLEEP_GPIO); CHECK(!o.wake_on_closed); CHECK_EQ(o.pin_mask, 1);
}

PPE_TEST(sleep_helmet_watches_both_contacts_and_both_directions, "TC-15") {
  ppe_sleep_state_t s; ppe_sleep_state_init(&s); ppe_sleep_out_t o;
  ppe_sleep_in_t a = in(PPE_NTYPE_HELMET, PPE_WEAR_NOT_WORN, false, false);
  ppe_sleep_decide(&s, &a, &o);
  CHECK_EQ(o.mode, PPE_SLEEP_GPIO); CHECK(o.wake_on_closed); CHECK_EQ(o.pin_mask, 3);
  ppe_sleep_in_t b = in(PPE_NTYPE_HELMET, PPE_WEAR_WORN, true, true);
  ppe_sleep_decide(&s, &b, &o);
  CHECK_EQ(o.mode, PPE_SLEEP_GPIO); CHECK(!o.wake_on_closed); CHECK_EQ(o.pin_mask, 3);
}

PPE_TEST(sleep_aborts_when_the_sensor_is_already_at_the_wake_level, "TC-15") {
  ppe_sleep_state_t s; ppe_sleep_state_init(&s); ppe_sleep_out_t o;
  ppe_sleep_in_t a = in(PPE_NTYPE_GLOVE, PPE_WEAR_NOT_WORN, true, false);           // a change arrived while preparing (N-07b)
  ppe_sleep_decide(&s, &a, &o);
  CHECK_EQ(o.mode, PPE_SLEEP_ABORT);
  ppe_sleep_in_t b = in(PPE_NTYPE_GLOVE, PPE_WEAR_WORN, false, false);
  ppe_sleep_decide(&s, &b, &o);
  CHECK_EQ(o.mode, PPE_SLEEP_ABORT);
  ppe_sleep_in_t c = in(PPE_NTYPE_HELMET, PPE_WEAR_WORN, true, false);              // one contact opened on a worn helmet
  ppe_sleep_decide(&s, &c, &o);
  CHECK_EQ(o.mode, PPE_SLEEP_ABORT);
}

PPE_TEST(sleep_helmet_with_one_contact_closed_goes_to_timer_poll_after_two_noop_wakes, "TC-15") {
  ppe_sleep_state_t s; ppe_sleep_state_init(&s); ppe_sleep_out_t o;
  ppe_sleep_in_t one = in(PPE_NTYPE_HELMET, PPE_WEAR_NOT_WORN, true, false);        // only one contact closed
  ppe_sleep_note_wake(&s, true, false);                                              // GPIO wake 1, no candidate change
  CHECK_EQ(s.gpio_noop_wakes, 1);
  ppe_sleep_decide(&s, &one, &o);
  CHECK_EQ(o.mode, PPE_SLEEP_ABORT);                                                 // sensor at the wake level: no sleep
  ppe_sleep_note_abort(&s);
  CHECK_EQ(s.gpio_noop_wakes, 2);
  ppe_sleep_decide(&s, &one, &o);
  CHECK_EQ(o.mode, PPE_SLEEP_POLL); CHECK(o.timer_valid); CHECK_EQ(o.timer_ms, 1000u); CHECK(s.poll_mode);
  ppe_sleep_note_wake(&s, false, false);                                             // the poll timer woke it: the count is cleared
  CHECK_EQ(s.gpio_noop_wakes, 0); CHECK(s.poll_mode);
  ppe_sleep_decide(&s, &one, &o);                                                    // still one contact: stays in poll mode
  CHECK_EQ(o.mode, PPE_SLEEP_POLL);
  ppe_sleep_in_t open = in(PPE_NTYPE_HELMET, PPE_WEAR_NOT_WORN, false, false);       // the contact opens: N-10b
  ppe_sleep_decide(&s, &open, &o);
  CHECK(!s.poll_mode); CHECK_EQ(s.gpio_noop_wakes, 0); CHECK_EQ(o.mode, PPE_SLEEP_GPIO);
}

PPE_TEST(sleep_a_flickering_pin_causes_at_most_two_consecutive_gpio_wakes_before_poll, "TC-15") {
  ppe_sleep_state_t s; ppe_sleep_state_init(&s); ppe_sleep_out_t o;
  ppe_sleep_in_t a = in(PPE_NTYPE_GLOVE, PPE_WEAR_NOT_WORN, false, false);          // armed fine each time, but the pin glitches
  int gpio_wakes = 0;
  for (int i = 0; i < 6; i++) {
    ppe_sleep_decide(&s, &a, &o);
    if (o.mode == PPE_SLEEP_POLL) break;
    CHECK_EQ(o.mode, PPE_SLEEP_GPIO);
    gpio_wakes++;
    ppe_sleep_note_wake(&s, true, false);                                            // woke on the glitch, nothing changed
  }
  CHECK_EQ(gpio_wakes, 2);
  CHECK(s.poll_mode);
}

PPE_TEST(sleep_a_real_change_resets_the_noop_count, "TC-15") {
  ppe_sleep_state_t s; ppe_sleep_state_init(&s);
  ppe_sleep_note_wake(&s, true, false);
  CHECK_EQ(s.gpio_noop_wakes, 1);
  ppe_sleep_note_wake(&s, true, true);                                               // a candidate change happened while awake
  CHECK_EQ(s.gpio_noop_wakes, 0);
  ppe_sleep_note_wake(&s, true, false); ppe_sleep_note_wake(&s, false, false);       // a timer wake clears it too
  CHECK_EQ(s.gpio_noop_wakes, 0);
}

PPE_TEST(sleep_foot_node_wakes_on_a_timer_to_sample_and_never_on_a_gpio, "TC-14") {
  ppe_sleep_state_t s; ppe_sleep_state_init(&s); ppe_sleep_out_t o;
  ppe_sleep_in_t a = in(PPE_NTYPE_FOOT, PPE_WEAR_WORN, false, false, true, 300000);
  ppe_sleep_decide(&s, &a, &o);
  CHECK_EQ(o.mode, PPE_SLEEP_POLL); CHECK(o.timer_valid); CHECK_EQ(o.timer_ms, 1000u); CHECK_EQ(o.pin_mask, 0);
  a.hb_remaining_ms = 400;                                                           // the heartbeat is due sooner than the next sample
  ppe_sleep_decide(&s, &a, &o);
  CHECK_EQ(o.timer_ms, 400u);
  a.hb_set = false;
  ppe_sleep_decide(&s, &a, &o);
  CHECK_EQ(o.timer_ms, 1000u);
}

PPE_TEST(sleep_timer_follows_the_heartbeat_deadline_and_poll_period, "TC-09") {
  ppe_sleep_state_t s; ppe_sleep_state_init(&s); ppe_sleep_out_t o;
  ppe_sleep_in_t a = in(PPE_NTYPE_GLOVE, PPE_WEAR_NOT_WORN, false, false, true, 123456);
  ppe_sleep_decide(&s, &a, &o);
  CHECK_EQ(o.mode, PPE_SLEEP_GPIO); CHECK_EQ(o.timer_ms, 123456u);
  a.hb_set = false;                                                                  // before the first sequence: no heartbeat timer (A-07)
  ppe_sleep_decide(&s, &a, &o);
  CHECK_EQ(o.mode, PPE_SLEEP_GPIO); CHECK(!o.timer_valid);
  s.poll_mode = true; a.hb_set = true; a.hb_remaining_ms = 250; a.contact_a_closed = true;   // poll mode lasts while the sensor stays at the wake level
  ppe_sleep_decide(&s, &a, &o);
  CHECK_EQ(o.mode, PPE_SLEEP_POLL); CHECK_EQ(o.timer_ms, 250u);                      // heartbeat sooner than the poll period
}

PPE_TEST(sleep_a_faulted_node_arms_no_wake_source_at_all, "TC-17") {
  ppe_sleep_state_t s; ppe_sleep_state_init(&s); ppe_sleep_out_t o;
  ppe_sleep_in_t a = in(PPE_NTYPE_GLOVE, PPE_WEAR_NOT_WORN, false, false);
  a.node_fault = true;
  ppe_sleep_decide(&s, &a, &o);
  CHECK_EQ(o.mode, PPE_SLEEP_FOREVER); CHECK(!o.timer_valid); CHECK_EQ(o.pin_mask, 0);
  a.type = PPE_NTYPE_FOOT;
  ppe_sleep_decide(&s, &a, &o);
  CHECK_EQ(o.mode, PPE_SLEEP_FOREVER); CHECK(!o.timer_valid);
}
