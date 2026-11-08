// S2 host tests: settle filter, confirm/grace with an injected clock, heartbeat guard, whole pipeline.
#include "ppe_test.h"
#include "ppe_sense.h"
#include "ppe_settle.h"
#include "ppe_confirm.h"
#include "ppe_config.h"

static ppe_raw_t NW() { return ppe_raw_make(PPE_WEAR_NOT_WORN, 0); }
static ppe_raw_t WN() { return ppe_raw_make(PPE_WEAR_WORN, 0); }

// ---- settle (TC-05) ------------------------------------------------------------------------------------------

PPE_TEST(settle_zero_makes_the_candidate_in_the_same_poll, "TC-05") {
  for (int i = 0; i < 20; i++) {
    ppe_settle_t s; ppe_settle_init(&s, NW());
    CHECK(ppe_settle_step(&s, 1000u + (uint32_t)i * 50u, WN(), 0));
    CHECK(ppe_raw_eq(s.candidate, WN()));
    CHECK(!ppe_settle_step(&s, 1050u + (uint32_t)i * 50u, WN(), 0));   // no second change
  }
}

PPE_TEST(settle_500_a_400_ms_pulse_gives_no_candidate, "TC-05") {
  for (int trial = 0; trial < 10; trial++) {
    ppe_settle_t s; ppe_settle_init(&s, NW());
    for (uint32_t t = 0; t <= 350; t += 50) CHECK(!ppe_settle_step(&s, t, WN(), 500));
    CHECK(!ppe_settle_step(&s, 400, NW(), 500));         // back to the candidate before 500 ms
    CHECK(!s.pending);
    for (uint32_t t = 450; t <= 2000; t += 50) CHECK(!ppe_settle_step(&s, t, NW(), 500));
    CHECK(ppe_raw_eq(s.candidate, NW()));
  }
}

PPE_TEST(settle_500_a_600_ms_change_gives_a_candidate_at_500, "TC-05") {
  ppe_settle_t s; ppe_settle_init(&s, NW());
  int changes = 0; uint32_t at = 0;
  for (uint32_t t = 1000; t <= 1600; t += 50)
    if (ppe_settle_step(&s, t, WN(), 500)) { changes++; at = t - 1000; }
  CHECK_EQ(changes, 1); CHECK_EQ(at, 500);
  CHECK(ppe_raw_eq(s.candidate, WN()));
}

PPE_TEST(settle_survives_the_millisecond_counter_wrap, "TC-05") {
  ppe_settle_t s; ppe_settle_init(&s, NW());
  uint32_t t0 = 0xFFFFFF00u; int changes = 0; uint32_t at = 0;
  for (uint32_t k = 0; k <= 20; k++)
    if (ppe_settle_step(&s, t0 + k * 50u, WN(), 500)) { changes++; at = k * 50u; }
  CHECK_EQ(changes, 1); CHECK_EQ(at, 500);
}

PPE_TEST(settle_applies_to_the_pair_so_a_fault_change_is_filtered_too, "TC-05") {
  ppe_settle_t s; ppe_settle_init(&s, NW());
  ppe_raw_t flt = ppe_raw_make(PPE_WEAR_NOT_WORN, 1);
  CHECK(!ppe_settle_step(&s, 0, flt, 300));
  CHECK(!ppe_settle_step(&s, 250, flt, 300));
  CHECK(ppe_settle_step(&s, 300, flt, 300));
  CHECK(ppe_raw_eq(s.candidate, flt));
}

// ---- confirm / grace (TC-06, TC-08) --------------------------------------------------------------------------

PPE_TEST(cold_start_confirms_after_the_boot_grace_with_the_boot_flag, "TC-08") {
  ppe_confirm_t c; ppe_event_t ev; ppe_confirm_cold_start(&c, 0);
  CHECK_EQ(c.confirmed.state, PPE_WEAR_NONE);
  int events = 0; uint32_t at = 0;
  for (uint32_t t = 0; t <= 60000; t += 50)
    if (ppe_confirm_tick(&c, t, WN(), 25, &ev)) { events++; at = t; CHECK(ev.boot); CHECK(ppe_raw_eq(ev.pair, WN())); }
  CHECK_EQ(events, 1); CHECK_EQ(at, 25000);
  CHECK(ppe_raw_eq(c.confirmed, WN()));
}

PPE_TEST(cold_start_a_change_restarts_the_boot_grace_every_time, "TC-08") {
  ppe_confirm_t c; ppe_event_t ev; ppe_settle_t s;
  ppe_confirm_cold_start(&c, 0); ppe_settle_init(&s, NW());
  int events = 0; uint32_t at = 0; ppe_raw_t last = NW();
  for (uint32_t t = 0; t <= 80000; t += 50) {
    ppe_raw_t raw = (t >= 20000) ? NW() : (t >= 10000 ? WN() : NW());   // change at 10 s and again at 20 s
    if (ppe_settle_step(&s, t, raw, 0)) ppe_confirm_on_candidate(&c, t, s.candidate);
    if (ppe_confirm_tick(&c, t, s.candidate, 25, &ev)) { events++; at = t; last = ev.pair; CHECK(ev.boot); }
  }
  CHECK_EQ(events, 1); CHECK_EQ(at, 45000); CHECK(ppe_raw_eq(last, NW()));
}

PPE_TEST(grace_a_change_held_gives_exactly_one_event_at_grace_s, "TC-06") {
  ppe_confirm_t c; ppe_settle_t s;
  ppe_confirm_resume(&c, NW(), false, false, 0); ppe_settle_init(&s, NW());
  uint32_t base = 1000;
  for (int trial = 0; trial < 10; trial++) {             // 10 trials, both directions
    ppe_raw_t raw = (trial % 2 == 0) ? WN() : NW();
    int events = 0; uint32_t at = 0;
    for (uint32_t k = 0; k <= 700; k++) {
      uint32_t now = base + k * 50u; ppe_event_t ev;
      if (ppe_settle_step(&s, now, raw, 0)) ppe_confirm_on_candidate(&c, now, s.candidate);
      if (ppe_confirm_tick(&c, now, s.candidate, 25, &ev)) { events++; at = now - base; CHECK(ppe_raw_eq(ev.pair, raw)); CHECK(!ev.boot); }
    }
    CHECK_EQ(events, 1); CHECK_EQ(at, 25000);
    base += 701u * 50u;
  }
}

PPE_TEST(grace_b_a_revert_before_expiry_sends_nothing, "TC-06") {
  ppe_confirm_t c; ppe_settle_t s;
  ppe_confirm_resume(&c, NW(), false, false, 0); ppe_settle_init(&s, NW());
  int events = 0; bool cancelled = false;
  for (uint32_t t = 0; t <= 60000; t += 50) {
    ppe_raw_t raw = (t >= 10000) ? NW() : (t >= 500 ? WN() : NW());
    ppe_event_t ev;
    if (ppe_settle_step(&s, t, raw, 0)) { ppe_confirm_on_candidate(&c, t, s.candidate); if (!c.in_grace) cancelled = true; }
    if (ppe_confirm_tick(&c, t, s.candidate, 25, &ev)) events++;
  }
  CHECK(cancelled); CHECK_EQ(events, 0);
  CHECK(ppe_raw_eq(c.confirmed, NW()));
}

PPE_TEST(grace_c_revert_then_change_starts_a_full_new_timer, "TC-06") {
  ppe_confirm_t c; ppe_settle_t s;
  ppe_confirm_resume(&c, NW(), false, false, 0); ppe_settle_init(&s, NW());
  int events = 0; uint32_t at = 0;
  for (uint32_t t = 0; t <= 60000; t += 50) {
    ppe_raw_t raw = (t >= 12000) ? WN() : (t >= 10000 ? NW() : (t >= 50 ? WN() : NW()));   // change, revert at 10 s, change at 12 s
    ppe_event_t ev;
    if (ppe_settle_step(&s, t, raw, 0)) ppe_confirm_on_candidate(&c, t, s.candidate);
    if (ppe_confirm_tick(&c, t, s.candidate, 25, &ev)) { events++; at = t; }
  }
  CHECK_EQ(events, 1); CHECK_EQ(at, 37000);              // 12 s plus 25 s
}

PPE_TEST(grace_d_short_and_limit_values_of_grace_s, "TC-06") {
  const uint16_t graces[] = {1, 10, 120};
  for (int i = 0; i < 3; i++) {
    ppe_confirm_t c; ppe_event_t ev;
    ppe_confirm_resume(&c, NW(), false, false, 0);
    ppe_confirm_on_candidate(&c, 5, WN());
    uint32_t want = 5u + (uint32_t)graces[i] * 1000u; int events = 0; uint32_t at = 0;
    for (uint32_t t = 5; t <= want + 500; t += 50)
      if (ppe_confirm_tick(&c, t, WN(), graces[i], &ev)) { events++; at = t; }
    CHECK_EQ(events, 1); CHECK(at >= want && at < want + 50);
  }
}

PPE_TEST(grace_the_timer_survives_the_millisecond_counter_wrap, "TC-06") {
  ppe_confirm_t c; ppe_event_t ev;
  ppe_confirm_resume(&c, NW(), false, false, 0);
  uint32_t t0 = 0xFFFFFF00u; ppe_confirm_on_candidate(&c, t0, WN());
  CHECK(!ppe_confirm_tick(&c, t0 + 24950u, WN(), 25, &ev));
  CHECK(ppe_confirm_tick(&c, t0 + 25000u, WN(), 25, &ev));
}

PPE_TEST(grace_a_third_value_during_grace_keeps_the_timer_and_confirms_the_latest, "TC-06") {
  ppe_confirm_t c; ppe_event_t ev;
  ppe_confirm_resume(&c, WN(), false, false, 0);
  ppe_raw_t flt = ppe_raw_make(PPE_WEAR_NOT_WORN, 1);
  ppe_confirm_on_candidate(&c, 0, NW());
  ppe_confirm_on_candidate(&c, 5000, flt);               // still differs from confirmed
  CHECK(c.in_grace); CHECK_EQ(c.grace_start, 0);
  CHECK(!ppe_confirm_tick(&c, 24950, flt, 25, &ev));
  CHECK(ppe_confirm_tick(&c, 25000, flt, 25, &ev));
  CHECK(ppe_raw_eq(ev.pair, flt));
}

PPE_TEST(an_event_is_requested_once_and_the_boot_flag_is_used_once, "TC-06") {
  ppe_confirm_t c; ppe_event_t ev;
  ppe_confirm_cold_start(&c, 0);
  CHECK(ppe_confirm_tick(&c, 25000, NW(), 25, &ev)); CHECK(ev.boot);
  for (uint32_t t = 25050; t < 100000; t += 50) CHECK(!ppe_confirm_tick(&c, t, NW(), 25, &ev));
  ppe_confirm_on_candidate(&c, 100000, WN());
  CHECK(ppe_confirm_tick(&c, 125000, WN(), 25, &ev)); CHECK(!ev.boot);
}

// ---- heartbeat guard (A-07) ---------------------------------------------------------------------------------

PPE_TEST(heartbeat_is_never_due_while_confirmed_is_none, "TC-09") {
  ppe_confirm_t c; ppe_confirm_cold_start(&c, 0);
  CHECK(!c.hb_set);
  for (uint32_t t = 0; t <= 200000; t += 1000) CHECK(!ppe_hb_due(&c, t));    // hb_deadline unset at cold start
  ppe_hb_on_sequence_start(&c, 0, 60);                                       // even with a deadline, NONE blocks it
  for (uint32_t t = 0; t <= 200000; t += 1000) CHECK(!ppe_hb_due(&c, t));
}

PPE_TEST(heartbeat_grace_longer_than_heartbeat_sends_no_heartbeat_in_boot_grace, "TC-09") {
  ppe_confirm_t c; ppe_event_t ev; ppe_confirm_cold_start(&c, 0);
  const uint16_t grace = 120, hb = 60;
  bool hb_seen = false, event = false;
  for (uint32_t t = 0; t <= 130000; t += 50) {
    if (ppe_hb_due(&c, t)) hb_seen = true;
    if (ppe_confirm_tick(&c, t, WN(), grace, &ev)) { event = true; ppe_hb_on_sequence_start(&c, t, hb); }
  }
  CHECK(!hb_seen); CHECK(event);
  CHECK(!ppe_hb_due(&c, 130000));                        // 120 s + 60 s not reached yet
  CHECK(ppe_hb_due(&c, 180000));
}

PPE_TEST(heartbeat_due_exactly_at_the_deadline_and_wrap_safe, "TC-09") {
  ppe_confirm_t c; ppe_event_t ev;
  ppe_confirm_resume(&c, WN(), false, false, 0);
  CHECK(!ppe_hb_due(&c, 5000000u));                      // no deadline yet
  ppe_hb_on_sequence_start(&c, 0xFFFFFF00u, 60);
  uint32_t due = 0xFFFFFF00u + 60000u;
  CHECK(!ppe_hb_due(&c, due - 1)); CHECK(ppe_hb_due(&c, due)); CHECK(ppe_hb_due(&c, due + 5000));
  (void)ev;
}

// ---- whole pipeline: conversion -> settle -> confirm ---------------------------------------------------------

PPE_TEST(pipeline_fsr_boots_in_the_dead_band_then_gets_worn, "TC-06") {
  ppe_fsr_t f; ppe_fsr_init(&f);
  ppe_raw_t raw0 = ppe_conv_fsr(&f, true, 800);
  ppe_settle_t s; ppe_settle_init(&s, raw0);
  ppe_confirm_t c; ppe_confirm_cold_start(&c, 0);
  int events = 0; uint32_t at[3] = {0, 0, 0}; bool boot[3] = {false, false, false}; ppe_raw_t got[3] = {NW(), NW(), NW()};
  for (uint32_t t = 50; t <= 90000; t += 50) {
    uint32_t count = (t >= 30000) ? 2000u : 800u;
    ppe_raw_t raw = ppe_conv_fsr(&f, true, count);
    ppe_event_t ev;
    if (ppe_settle_step(&s, t, raw, 0)) ppe_confirm_on_candidate(&c, t, s.candidate);
    if (ppe_confirm_tick(&c, t, s.candidate, 25, &ev) && events < 3) { at[events] = t; boot[events] = ev.boot; got[events] = ev.pair; events++; }
  }
  CHECK_EQ(events, 2);
  CHECK_EQ(at[0], 25000); CHECK(boot[0]); CHECK(ppe_raw_eq(got[0], NW()));
  CHECK_EQ(at[1], 55000); CHECK(!boot[1]); CHECK(ppe_raw_eq(got[1], WN()));
}

PPE_TEST(pipeline_sensor_fault_event_and_recovery_from_worn_and_not_worn, "TC-16") {
  for (int start = 0; start < 2; start++) {
    const bool worn = (start == 0);
    ppe_fsr_t f; ppe_fsr_init(&f); ppe_fsr_seed(&f, worn ? PPE_WEAR_WORN : PPE_WEAR_NOT_WORN);
    const uint32_t count = worn ? 2000u : 50u;
    ppe_settle_t s; ppe_settle_init(&s, worn ? WN() : NW());
    ppe_confirm_t c; ppe_confirm_resume(&c, worn ? WN() : NW(), false, false, 0);
    int events = 0; uint32_t at[3] = {0, 0, 0}; ppe_raw_t got[3] = {NW(), NW(), NW()};
    for (uint32_t t = 0; t <= 100000; t += 50) {
      bool ok = !(t >= 1000 && t < 40000);               // adc fail from 1 s to 40 s
      ppe_raw_t raw = ppe_conv_fsr(&f, ok, count);
      ppe_event_t ev;
      if (ppe_settle_step(&s, t, raw, 0)) ppe_confirm_on_candidate(&c, t, s.candidate);
      if (ppe_confirm_tick(&c, t, s.candidate, 25, &ev) && events < 3) { at[events] = t; got[events] = ev.pair; events++; }
    }
    // WORN start: fault gives (NOT_WORN,1) at 26 s, recovery gives (WORN,0) at 65 s.
    // NOT_WORN start: fault gives (NOT_WORN,1) at 26 s, recovery gives (NOT_WORN,0) at 65 s.
    CHECK_EQ(events, 2);
    CHECK_EQ(at[0], 26000); CHECK_EQ(got[0].state, PPE_WEAR_NOT_WORN); CHECK_EQ(got[0].fault, 1);
    CHECK_EQ(at[1], 65000); CHECK_EQ(got[1].state, worn ? PPE_WEAR_WORN : PPE_WEAR_NOT_WORN); CHECK_EQ(got[1].fault, 0);
  }
}

// ---- config struct and bounds ---------------------------------------------------------------------------------

PPE_TEST(config_defaults_and_bounds_reject_without_changing_anything, "TC-06") {
  ppe_cfg_t c; ppe_cfg_defaults(&c);
  CHECK_EQ(c.settle_ms, 0); CHECK_EQ(c.grace_s, 25); CHECK_EQ(c.heartbeat_s, 360);
  CHECK(!ppe_cfg_set_grace(&c, 0));    CHECK(!ppe_cfg_set_grace(&c, 121)); CHECK(!ppe_cfg_set_grace(&c, -1));
  CHECK(!ppe_cfg_set_settle(&c, -1));  CHECK(!ppe_cfg_set_settle(&c, 1001));
  CHECK(!ppe_cfg_set_heartbeat(&c, 59)); CHECK(!ppe_cfg_set_heartbeat(&c, 3601));
  CHECK_EQ(c.settle_ms, 0); CHECK_EQ(c.grace_s, 25); CHECK_EQ(c.heartbeat_s, 360);
  CHECK(ppe_cfg_set_grace(&c, 1));  CHECK(ppe_cfg_set_grace(&c, 120));
  CHECK(ppe_cfg_set_settle(&c, 0)); CHECK(ppe_cfg_set_settle(&c, 1000));
  CHECK(ppe_cfg_set_heartbeat(&c, 60)); CHECK(ppe_cfg_set_heartbeat(&c, 3600));
  CHECK_EQ(c.grace_s, 120); CHECK_EQ(c.settle_ms, 1000); CHECK_EQ(c.heartbeat_s, 3600);
}
