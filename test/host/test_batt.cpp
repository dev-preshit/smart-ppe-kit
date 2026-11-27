// S4 host tests: battery percent and LOW_BATT (FR-13, protocol 6.1 battery byte, T-03). TC-13 runs injected-only (A-26).
#include "ppe_test.h"
#include "ppe_batt.h"
#include "ppe_node.h"
#include "ppe_proto.h"
#include "ppe_filter.h"
#include "ppe_test_key.h"
#include <string.h>

PPE_TEST(batt_table_points_and_clamps, "TC-13") {
  CHECK_EQ(ppe_batt_pct_from_mv(4200), 100);
  CHECK_EQ(ppe_batt_pct_from_mv(5000), 100);            // above full: clamped
  CHECK_EQ(ppe_batt_pct_from_mv(3300), 0);
  CHECK_EQ(ppe_batt_pct_from_mv(0), 0);                 // below empty: clamped, still a valid reading
  CHECK_EQ(ppe_batt_pct_from_mv(3900), 70);
  CHECK_EQ(ppe_batt_pct_from_mv(3600), 25);
  CHECK_EQ(ppe_batt_pct_from_mv(3650), 32);             // linear between table points (25 + 15 / 2, rounded down)
}

PPE_TEST(batt_percent_is_non_increasing_as_the_voltage_falls_and_stays_in_range, "TC-13") {
  int prev = 101;
  for (int mv = 5000; mv >= 0; mv--) {
    int p = ppe_batt_pct_from_mv((uint16_t)mv);
    CHECK(p >= 0 && p <= 100);
    CHECK(p <= prev);
    prev = p;
  }
}

PPE_TEST(batt_tc13_bench_points_give_the_expected_flags, "TC-13") {
  const uint16_t mv[4] = {4200, 3900, 3600, 3300};      // the four TC-13 supply steps
  const bool low[4] = {false, false, false, true};
  for (int i = 0; i < 4; i++) {
    uint8_t p = ppe_batt_report(PPE_BATT_SRC_INJECTED, mv[i]);
    CHECK_EQ(ppe_low_batt(p), low[i]);
  }
  CHECK_EQ(ppe_batt_pct_from_mv(3520), 17);
  CHECK(ppe_low_batt(ppe_batt_pct_from_mv(3520)));      // 17 % -> LOW_BATT
  CHECK(!ppe_low_batt(ppe_batt_pct_from_mv(3560)));     // 21 % -> no LOW_BATT
}

PPE_TEST(batt_unavailable_or_failed_read_is_0xff_without_low_batt, "TC-13") {
  CHECK_EQ(ppe_batt_report(PPE_BATT_SRC_FAIL, 3700), PPE_BATT_NA);
  CHECK_EQ(ppe_batt_report(PPE_BATT_SRC_NONE, 3700), PPE_BATT_NA);   // no divider on this board (A-26): not available
  CHECK(!ppe_low_batt(PPE_BATT_NA));
  CHECK_EQ(ppe_batt_report(PPE_BATT_SRC_INJECTED, 3700), 40);
}

PPE_TEST(batt_byte_and_low_batt_flag_reach_the_packet, "TC-13") {
  ppe_ident_t id; ppe_ident_clear(&id);
  id.node_id = 1; id.vest_id = 1; memcpy(id.key, PPE_TEST_KEY, 32); id.has_key = true;
  uint8_t adv[PPE_ADV_LEN] = {0}; ppe_packet_t p;
  CHECK(ppe_build_adv(&id, PPE_HEARTBEAT, false, false, PPE_WEAR_WORN, ppe_batt_report(PPE_BATT_SRC_INJECTED, 3300), 10, adv));
  CHECK_EQ(ppe_rx_filter(true, adv, PPE_ADV_LEN, 1, PPE_TEST_KEY, &p), PPE_RX_OK);
  CHECK_EQ(p.battery, 0); CHECK((p.flags & PPE_FLAG_LOW_BATT) != 0);
  CHECK(ppe_build_adv(&id, PPE_HEARTBEAT, false, false, PPE_WEAR_WORN, ppe_batt_report(PPE_BATT_SRC_FAIL, 0), 11, adv));
  CHECK_EQ(ppe_rx_filter(true, adv, PPE_ADV_LEN, 1, PPE_TEST_KEY, &p), PPE_RX_OK);
  CHECK_EQ(p.battery, 0xFF); CHECK((p.flags & PPE_FLAG_LOW_BATT) == 0);
}
