// S3 story 3: scan supervisor decisions with a fake clock (TC-21 host part; FR-21).
#include "ppe_test.h"
#include "hub_scan_supervisor.h"

struct Sim {
  HubScanSupervisor s; uint32_t t; bool healthy; uint32_t adv;
  int fail_restarts;        // the next N restart calls fail (the hub "stall n" hook)
  int restarts; int reboots; uint32_t first_restart_ms, reboot_ms;
  Sim() : t(0), healthy(true), adv(0), fail_restarts(0), restarts(0), reboots(0), first_restart_ms(0), reboot_ms(0) { s.start(0, 0); }
  void step() {             // one 1 s poll
    t += HUB_SCAN_POLL_MS;
    HubScanTick k = s.tick(t, healthy, adv);
    if (k.action == HUB_SCAN_RESTART) {
      restarts++; if (restarts == 1) first_restart_ms = t;
      if (fail_restarts > 0) { fail_restarts--; s.restart_done(t, false); }
      else { s.restart_done(t, true); healthy = true; }       // a working restart heals the scanner
    } else if (k.action == HUB_SCAN_REBOOT) { if (!reboots) reboot_ms = t; reboots++; }
  }
};

PPE_TEST(stall_one_failed_restart_then_recovers_without_reboot, "TC-21") {
  Sim m; m.step(); m.healthy = false; m.fail_restarts = 1;
  m.step(); uint32_t t_seen = m.t;                     // first 1 s poll that sees the stall
  for (int i = 0; i < 40; i++) m.step();
  CHECK_EQ(m.reboots, 0); CHECK_EQ(m.restarts, 2);
  CHECK_EQ(m.first_restart_ms - t_seen, 10000);        // restart 10 s after the first poll that saw it
  CHECK(m.first_restart_ms - t_seen + HUB_SCAN_POLL_MS <= 11000);   // i.e. 10-11 s after the real onset
  CHECK(m.healthy); CHECK_EQ(m.s.consecutive_fails(), 0);
}

PPE_TEST(stall_three_failed_restarts_reboot, "TC-21") {
  Sim m; m.step(); m.healthy = false; m.fail_restarts = 3;
  m.step(); uint32_t t_seen = m.t;
  for (int i = 0; i < 40; i++) m.step();
  CHECK_EQ(m.restarts, 3); CHECK_EQ(m.reboots > 0, 1);
  CHECK_EQ(m.reboot_ms - t_seen, 13000);               // restarts at +10, +11, +12 s, reboot decision at +13 s
}

PPE_TEST(restart_that_starts_but_shows_no_progress_counts_as_failed, "TC-21") {
  HubScanSupervisor s; s.start(0, 0); uint32_t t = 0; int restarts = 0, reboots = 0; uint32_t reboot_t = 0;
  for (int i = 0; i < 60; i++) {
    t += 1000; HubScanTick k = s.tick(t, false, 0);   // the scanner never becomes healthy
    if (k.action == HUB_SCAN_RESTART) { restarts++; s.restart_done(t, true); }   // restart call "works"
    if (k.action == HUB_SCAN_REBOOT && !reboots) { reboots++; reboot_t = t; }
  }
  CHECK_EQ(restarts, 3); CHECK_EQ(reboots, 1);
  CHECK_EQ(reboot_t, 41000);                           // first poll that saw it (1 s) + 10 s detect + 3 x 10 s without progress
}

PPE_TEST(unhealthy_for_less_than_10s_does_not_restart, "TC-21") {
  Sim m; m.step();
  for (int round = 0; round < 5; round++) {
    m.healthy = false; for (int i = 0; i < 9; i++) m.step();     // 9 s down, then it recovers by itself
    m.healthy = true; m.step();
  }
  CHECK_EQ(m.restarts, 0); CHECK_EQ(m.reboots, 0);
}

PPE_TEST(healthy_progress_clears_failure_count, "TC-21") {
  Sim m; m.step(); m.healthy = false; m.fail_restarts = 2;
  for (int i = 0; i < 13; i++) m.step();               // two failed restarts, third works
  CHECK_EQ(m.restarts, 3); CHECK(m.healthy);
  m.step(); CHECK_EQ(m.s.consecutive_fails(), 0);      // a new stall starts from zero again
  m.healthy = false; m.fail_restarts = 2;
  for (int i = 0; i < 20; i++) m.step();
  CHECK_EQ(m.reboots, 0);
}

PPE_TEST(no_advertisements_for_ten_minutes_is_not_a_stall, "TC-21") {
  Sim m;                                               // scanner healthy, zero advertisements
  for (int i = 0; i < 600; i++) m.step();
  CHECK_EQ(m.restarts, 0); CHECK_EQ(m.reboots, 0);
  CHECK_EQ(m.s.quiet_windows(), 10);                   // one quiet window per minute, informational only
}

PPE_TEST(quiet_window_not_counted_when_any_advertisement_seen, "TC-21") {
  Sim m;
  for (int i = 0; i < 300; i++) { if (i % 50 == 0) m.adv++; m.step(); }   // one advert every 50 s
  CHECK_EQ(m.s.quiet_windows(), 0);
  CHECK_EQ(m.restarts, 0);
}

PPE_TEST(supervisor_survives_32bit_ms_wrap, "TC-21") {
  HubScanSupervisor s; uint32_t t = 0xFFFFFC00u; s.start(t, 0); int restarts = 0;
  for (int i = 0; i < 30; i++) { t += 1000; HubScanTick k = s.tick(t, false, 0);
    if (k.action == HUB_SCAN_RESTART) { restarts++; s.restart_done(t, true); } }
  CHECK_EQ(restarts, 2);                                // +10 s and +20 s, across the wrap, no early restart
}
