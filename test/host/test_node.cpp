// S4 host tests: node identity and packet builder. The reference decoder is the hub's own filter (ppe_rx_filter), so a packet
// built here is checked by exactly the code that receives it.
#include "ppe_test.h"
#include "ppe_node.h"
#include "ppe_filter.h"
#include "ppe_crypto.h"
#include "ppe_test_key.h"
#include <string.h>

static ppe_ident_t ident(uint8_t node, uint8_t vest) {
  ppe_ident_t id; ppe_ident_clear(&id);
  id.node_id = node; id.vest_id = vest; memcpy(id.key, PPE_TEST_KEY, 32); id.has_key = true;
  return id;
}

PPE_TEST(node_ident_is_valid_only_with_node_1_to_5_vest_1_to_254_and_a_key, "TC-01") {
  ppe_ident_t id = ident(1, 1);
  CHECK(ppe_ident_valid(&id));
  for (int n = 0; n < 256; n++) { id = ident((uint8_t)n, 7); CHECK_EQ(ppe_ident_valid(&id), n >= 1 && n <= 5); }
  for (int v = 0; v < 256; v++) { id = ident(3, (uint8_t)v); CHECK_EQ(ppe_ident_valid(&id), v >= 1 && v <= 254); }
  id = ident(3, 7); id.has_key = false;  CHECK(!ppe_ident_valid(&id));
  ppe_ident_clear(&id);                  CHECK(!ppe_ident_valid(&id));      // blank board: UNPROVISIONED
  CHECK(!ppe_ident_valid(nullptr));
}

PPE_TEST(node_built_packet_is_accepted_by_the_hub_filter_with_the_right_fields, "TC-07") {
  ppe_ident_t id = ident(3, 9);
  uint8_t adv[PPE_ADV_LEN] = {0};
  CHECK(ppe_build_adv(&id, PPE_EVENT, false, false, PPE_WEAR_WORN, 0xFF, 259, adv));
  CHECK_EQ(adv[0], 0x14); CHECK_EQ(adv[1], 0xFF); CHECK_EQ(adv[2], 0xFF); CHECK_EQ(adv[3], 0xFF);
  ppe_packet_t p;
  CHECK_EQ(ppe_rx_filter(true, adv, PPE_ADV_LEN, 9, PPE_TEST_KEY, &p), PPE_RX_OK);
  CHECK_EQ(p.node_id, 3); CHECK_EQ(p.vest_id, 9); CHECK_EQ(p.state, PPE_WORN);
  CHECK_EQ(p.counter, 259u); CHECK_EQ(p.battery, 0xFF);
  CHECK_EQ(ppe_flags_version(p.flags), 1); CHECK((p.flags & PPE_FLAG_TYPE_EVENT) != 0);
  CHECK((p.flags & PPE_FLAG_BOOT) == 0); CHECK((p.flags & PPE_FLAG_LOW_BATT) == 0); CHECK((p.flags & PPE_FLAG_SENSOR_FAULT) == 0);
}

PPE_TEST(node_flags_follow_type_boot_low_battery_and_fault, "TC-13") {
  ppe_ident_t id = ident(1, 1);
  uint8_t adv[PPE_ADV_LEN]; ppe_packet_t p;
  CHECK(ppe_build_adv(&id, PPE_HEARTBEAT, false, false, PPE_WEAR_NOT_WORN, 100, 1, adv));
  CHECK_EQ(ppe_rx_filter(true, adv, PPE_ADV_LEN, 1, PPE_TEST_KEY, &p), PPE_RX_OK);
  CHECK_EQ(p.flags, 0x40);                                  // VER=01, heartbeat, nothing else
  CHECK_EQ(p.state, PPE_NOT_WORN);
  CHECK(ppe_build_adv(&id, PPE_EVENT, true, true, PPE_WEAR_WORN, 20, 2, adv));
  CHECK_EQ(ppe_rx_filter(true, adv, PPE_ADV_LEN, 1, PPE_TEST_KEY, &p), PPE_RX_OK);
  CHECK_EQ(p.flags, 0x40 | 0x20 | 0x10 | 0x08 | 0x04);     // event, BOOT, LOW_BATT (20 is low), SENSOR_FAULT
  CHECK(ppe_build_adv(&id, PPE_EVENT, false, false, PPE_WEAR_WORN, 21, 3, adv));
  CHECK_EQ(ppe_rx_filter(true, adv, PPE_ADV_LEN, 1, PPE_TEST_KEY, &p), PPE_RX_OK);
  CHECK((p.flags & PPE_FLAG_LOW_BATT) == 0);                // 21 is not low
  CHECK(ppe_build_adv(&id, PPE_HEARTBEAT, false, false, PPE_WEAR_WORN, 0xFF, 4, adv));
  CHECK_EQ(ppe_rx_filter(true, adv, PPE_ADV_LEN, 1, PPE_TEST_KEY, &p), PPE_RX_OK);
  CHECK((p.flags & PPE_FLAG_LOW_BATT) == 0);                // 0xFF = unreadable: not low
  CHECK_EQ(p.battery, 0xFF);
}

PPE_TEST(node_wear_none_is_never_put_on_the_wire, "TC-07") {
  CHECK_EQ(ppe_wear_to_wire(PPE_WEAR_WORN), PPE_WORN);
  CHECK_EQ(ppe_wear_to_wire(PPE_WEAR_NOT_WORN), PPE_NOT_WORN);
  CHECK_EQ(ppe_wear_to_wire(PPE_WEAR_NONE), PPE_NOT_WORN);
}

PPE_TEST(node_refuses_to_build_for_an_unprovisioned_identity, "TC-01") {
  ppe_ident_t id; ppe_ident_clear(&id);
  uint8_t adv[PPE_ADV_LEN]; memset(adv, 0xAA, sizeof adv);
  CHECK(!ppe_build_adv(&id, PPE_EVENT, false, false, PPE_WEAR_WORN, 50, 1, adv));
  for (size_t i = 0; i < sizeof adv; i++) CHECK_EQ(adv[i], 0xAA);      // output untouched
  id = ident(2, 2);
  CHECK(!ppe_build_adv(&id, PPE_EVENT, false, false, PPE_WEAR_WORN, 50, 1, nullptr));
}

PPE_TEST(node_counter_range_end_to_end_and_every_packet_differs_when_the_counter_differs, "TC-07") {
  ppe_ident_t id = ident(4, 5);
  uint8_t a[PPE_ADV_LEN], b[PPE_ADV_LEN]; ppe_packet_t p;
  const uint32_t cs[] = {1u, 255u, 256u, 258u, 65536u, 0x01000000u, 0xFFFFFFF0u};
  for (size_t i = 0; i < sizeof cs / sizeof cs[0]; i++) {
    CHECK(ppe_build_adv(&id, PPE_EVENT, false, false, PPE_WEAR_WORN, 80, cs[i], a));
    CHECK_EQ(ppe_rx_filter(true, a, PPE_ADV_LEN, 5, PPE_TEST_KEY, &p), PPE_RX_OK);
    CHECK_EQ(p.counter, cs[i]);
    CHECK(ppe_build_adv(&id, PPE_EVENT, false, false, PPE_WEAR_WORN, 80, cs[i] + 1u, b));
    CHECK(memcmp(a, b, sizeof a) != 0);
  }
}

PPE_TEST(node_wrong_key_or_vest_is_rejected_by_the_hub_filter, "TC-01") {
  ppe_ident_t id = ident(2, 6);
  uint8_t adv[PPE_ADV_LEN]; ppe_packet_t p;
  CHECK(ppe_build_adv(&id, PPE_EVENT, false, false, PPE_WEAR_WORN, 50, 10, adv));
  uint8_t other[32]; memcpy(other, PPE_TEST_KEY, 32); other[0] ^= 1;
  CHECK_EQ(ppe_rx_filter(true, adv, PPE_ADV_LEN, 6, other, &p), PPE_RX_HMAC_FAIL);
  CHECK_EQ(ppe_rx_filter(true, adv, PPE_ADV_LEN, 7, PPE_TEST_KEY, &p), PPE_RX_DROP_VEST);
}
