// S5 part 2, host part of TC-29 (motor alert), TC-30 (KEY_FAULT pattern) and TC-74 (motor safety): the motor FSM with
// a simulated clock, stepped every 1 ms. These do not replace the bench runs (the pin edges are recorded by the edge
// recorder, decision D8). Expected values come from state machines 2.6, spec 8.3, FR-29, FR-30, NFR-05 and D5.
#include "ppe_test.h"
#include <vector>
#include "hub_motor.h"
#include "hub_config.h"
#include "hub_hooks.h"

struct Edge { uint32_t t; bool level; };

struct Sim {
  HubMotor m; uint32_t t; std::vector<Edge> e; bool last, stall_pattern;
  explicit Sim(uint32_t t0 = 0) : t(t0), last(false), stall_pattern(false) {}
  void sample() { if (m.output() != last) { last = m.output(); Edge x = {t, last}; e.push_back(x); } }
  void alert(bool a) { m.set_alert(a, t); sample(); }
  void key(bool k) { m.set_key_fault(k, t); sample(); }
  void run(uint32_t us) {
    for (uint32_t k = 0; k < us; k += 1000) { t += 1000; if (!stall_pattern) m.pattern_step(t); m.guard_step(t); sample(); }
  }
};

static bool within(uint32_t v, uint32_t nominal, uint32_t tol) { return v + tol >= nominal && v <= nominal + tol; }

PPE_TEST(motor_is_low_after_construction_and_boot, "TC-74") {
  HubMotor m; CHECK(!m.output()); CHECK_EQ(m.state(), HubMotor::M_IDLE);
  m.set_alert(true, 5); CHECK(m.output());
  m.boot(); CHECK(!m.output()); CHECK_EQ(m.state(), HubMotor::M_IDLE); CHECK(!m.alert());   // M-07 / M-08: a restart starts low
}

PPE_TEST(alert_pattern_on_300_off_700_for_20_cycles, "TC-29") {
  Sim s(12345);
  s.alert(true);
  CHECK(s.m.output());                                          // M-01: starts at once (spec: within 200 ms)
  s.run(20 * 1000000u);
  CHECK(s.e.size() >= 40);
  unsigned on_n = 0, off_n = 0;
  for (size_t i = 1; i < s.e.size(); i++) {
    uint32_t d = s.e[i].t - s.e[i - 1].t;
    if (s.e[i - 1].level) { on_n++; CHECK(within(d, 300000, 1000)); } else { off_n++; CHECK(within(d, 700000, 1000)); }
    if (i + 1 < s.e.size() && !s.e[i].level) CHECK(within(s.e[i + 1].t - s.e[i - 1].t, 1000000, 1000));   // period 1 s, no drift
  }
  CHECK(on_n >= 19); CHECK(off_n >= 19);
  CHECK(s.m.max_on_us() <= 301000u);
  CHECK_EQ(s.m.guard_trips(), 0);
}

PPE_TEST(alert_clearing_stops_the_motor_at_once_in_on_and_in_off, "TC-29") {
  for (uint32_t clear_at = 50000; clear_at < 1100000; clear_at += 123000) {      // inside ON and inside OFF, several cycles
    Sim s(777);
    s.alert(true);
    s.run(clear_at);
    s.alert(false);
    CHECK(!s.m.output()); CHECK_EQ(s.m.state(), HubMotor::M_IDLE);               // M-06: low at once
    size_t edges = s.e.size();
    s.run(5000000);
    CHECK_EQ(s.e.size(), edges);                                                 // nothing more happens
  }
}

PPE_TEST(unknown_or_no_alert_never_starts_the_motor_and_realert_restarts_it, "TC-29") {
  Sim s;
  s.alert(false); s.run(3000000);
  CHECK_EQ(s.e.size(), 0);
  s.alert(true); s.run(1500000); s.alert(false); s.run(500000);
  size_t before = s.e.size();
  s.alert(true);
  CHECK(s.m.output());                                                           // a new alert starts a fresh ON at once
  CHECK_EQ(s.e.size(), before + 1);
}

PPE_TEST(repeated_alert_calls_do_not_restart_the_pattern, "TC-29") {
  Sim s;
  s.alert(true);
  for (int i = 0; i < 100; i++) { s.run(7000); s.alert(true); }                  // the glue calls it after every packet and tick
  s.run(5000000);
  for (size_t i = 1; i < s.e.size(); i++) if (s.e[i - 1].level) CHECK(within(s.e[i].t - s.e[i - 1].t, 300000, 1000));
}

PPE_TEST(key_fault_pattern_three_pulses_then_rest_period_2300_ms, "TC-30") {
  Sim s(4000);
  s.key(true);
  CHECK(s.m.output());                                                           // M-09: pattern starts at boot
  s.run(10 * 2300000u);
  unsigned groups = 0;
  for (size_t i = 0; i + 1 < s.e.size(); i++) {
    uint32_t d = s.e[i + 1].t - s.e[i].t;
    if (s.e[i].level) CHECK(within(d, 100000, 1000));                            // ON 100 ms
  }
  // rising edges: 3 per period, the group of three starts every 2300 ms
  std::vector<uint32_t> rises;
  for (size_t i = 0; i < s.e.size(); i++) if (s.e[i].level) rises.push_back(s.e[i].t);
  CHECK(rises.size() >= 27);
  for (size_t g = 0; g + 3 < rises.size(); g += 3) {
    CHECK(within(rises[g + 1] - rises[g], 200000, 1000)); CHECK(within(rises[g + 2] - rises[g + 1], 200000, 1000));   // OFF 100 ms between pulses
    CHECK(within(rises[g + 3] - rises[g], 2300000, 1000)); groups++;
  }
  CHECK(groups >= 8);
  // D5: the low time after the third ON is K_OFF + K_REST = 1800 ms (falling edge of the third ON to the next rise)
  std::vector<uint32_t> falls;
  for (size_t i = 0; i < s.e.size(); i++) if (!s.e[i].level) falls.push_back(s.e[i].t);
  CHECK(within(rises[3] - falls[2], 1800000, 1000));
  CHECK(s.m.max_on_us() <= 101000u);
}

PPE_TEST(key_fault_owns_the_motor_alert_never_starts_the_alert_pattern, "TC-30") {
  Sim s;
  s.key(true); s.alert(true);
  s.run(7000000);
  for (size_t i = 0; i + 1 < s.e.size(); i++) if (s.e[i].level) CHECK(within(s.e[i + 1].t - s.e[i].t, 100000, 1000));   // never a 300 ms ON
  CHECK_EQ(s.m.state() == HubMotor::M_ON || s.m.state() == HubMotor::M_OFF, false);
  s.alert(false);
  size_t n = s.e.size(); s.run(2300000);
  CHECK(s.e.size() > n);                                                         // the key pattern keeps running without alert
}

PPE_TEST(guard_forces_low_at_400_ms_when_the_pattern_task_is_stuck, "TC-74") {
  Sim s(1000);
  s.alert(true);
  s.stall_pattern = true;                                                        // pattern task hung right after the ON edge
  s.run(2000000);
  CHECK(s.m.guard_trips() >= 1);
  CHECK(s.m.max_on_us() >= 400000u && s.m.max_on_us() <= 401000u);               // output low by the guard at 400 ms
  CHECK(s.m.take_guard_trip()); CHECK(!s.m.take_guard_trip());                   // one report per trip (HUB_FAULT GUARD)
  CHECK(!s.m.output());
  s.stall_pattern = false;                                                       // the pattern resumes at the next ON edge
  size_t n = s.e.size(); s.run(1500000);
  CHECK(s.e.size() > n);
  CHECK(s.m.max_on_us() <= 401000u);
}

PPE_TEST(guard_also_limits_a_stuck_key_fault_pulse, "TC-74") {
  Sim s;
  s.key(true);
  s.stall_pattern = true;
  s.run(1000000);
  CHECK(s.m.max_on_us() >= 400000u && s.m.max_on_us() <= 401000u);
  CHECK(s.m.guard_trips() >= 1);
}

PPE_TEST(random_alert_toggling_with_stalls_never_exceeds_400_ms_on, "TC-74") {
  uint32_t rng = 987654321u;
  for (int run = 0; run < 20; run++) {
    Sim s(run == 0 ? 0xFFFFFFFFu - 3000000u : 1000u * run);                        // run 0 crosses the 32-bit wrap
    if (run % 4 == 3) s.key(true);
    for (int step = 0; step < 4000; step++) {
      rng = rng * 1664525u + 1013904223u;
      uint32_t r = (rng >> 16) % 100;
      if (r < 4) s.alert((rng >> 8) & 1);
      if (r == 5) s.stall_pattern = !s.stall_pattern;                              // the pattern task stalls and recovers at random
      if (r == 6 && run % 5 == 1) s.m.boot();                                      // a reset in the middle (M-08), output low
      if (r == 6 && run % 5 == 1) { CHECK(!s.m.output()); s.sample(); }
      s.run(((rng >> 4) % 50 + 1) * 1000u);
    }
    CHECK(s.m.max_on_us() <= 401000u);                                             // NFR-05 with 1 ms steps
    for (size_t i = 1; i < s.e.size(); i++) if (s.e[i - 1].level && !s.e[i].level) CHECK(s.e[i].t - s.e[i - 1].t <= 401000u);
  }
}

PPE_TEST(a_late_pattern_step_resynchronises_instead_of_bursting, "TC-74") {
  HubMotor m; m.set_alert(true, 0);
  m.pattern_step(300000u + 150000u);                                              // the pattern task was held up 150 ms
  CHECK(!m.output()); CHECK_EQ(m.state(), HubMotor::M_OFF);
  uint32_t dl; CHECK(m.pattern_deadline(&dl)); CHECK_EQ(dl, 450000u + 700000u);   // 700 ms OFF counted from the real edge
}

PPE_TEST(pattern_timing_is_exact_across_the_32_bit_microsecond_wrap, "TC-74") {
  Sim s(0xFFFFFFFFu - 1500000u);                                                   // the clock wraps 1.5 s into the run
  s.alert(true);
  s.run(10 * 1000000u);
  CHECK(s.e.size() >= 18);
  for (size_t i = 1; i < s.e.size(); i++) {
    uint32_t d = s.e[i].t - s.e[i - 1].t;
    if (s.e[i - 1].level) CHECK(within(d, 300000, 1000)); else CHECK(within(d, 700000, 1000));
  }
  CHECK_EQ(s.m.guard_trips(), 0);
}

PPE_TEST(motor_hook_parser, "TC-29") {
  CHECK_EQ(hub_hook_parse("motor").type, HUB_HOOK_MOTOR);
  CHECK_EQ(hub_hook_parse("  motor \r\n").type, HUB_HOOK_MOTOR);
  CHECK_EQ(hub_hook_parse("motor 1").type, HUB_HOOK_ERROR);
  CHECK_EQ(hub_hook_parse("motors").type, HUB_HOOK_ERROR);
  CHECK_EQ(hub_hook_parse("dump").type, HUB_HOOK_DUMP);
}
