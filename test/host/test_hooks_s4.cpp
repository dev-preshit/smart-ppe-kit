// S4 host tests: the node hooks added in S4 (test plan section 3). Compiled only with PPE_TEST_HOOKS.
#include "ppe_test.h"
#include "ppe_hooks.h"

#ifdef PPE_TEST_HOOKS

PPE_TEST(s4_hook_tx_sends_a_heartbeat_or_an_event_with_the_state, "TC-07") {
  ppe_hook_cmd_t h;
  CHECK_EQ(ppe_hook_parse("tx hb", &h), PPE_HOOK_TX_HB);
  CHECK_EQ(ppe_hook_parse("tx worn", &h), PPE_HOOK_TX_EVENT);     CHECK_EQ(h.arg, 1);
  CHECK_EQ(ppe_hook_parse("tx notworn", &h), PPE_HOOK_TX_EVENT);  CHECK_EQ(h.arg, 0);
  CHECK_EQ(ppe_hook_parse("tx maybe", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("tx", &h), PPE_HOOK_UNKNOWN);
  CHECK_EQ(ppe_hook_parse("tx hb now", &h), PPE_HOOK_UNKNOWN);
}

PPE_TEST(s4_hook_setcounter_takes_the_full_32_bit_range_and_nothing_else, "TC-12") {
  ppe_hook_cmd_t h;
  CHECK_EQ(ppe_hook_parse("setcounter 0", &h), PPE_HOOK_SETCOUNTER);           CHECK_EQ(h.uarg, 0u);
  CHECK_EQ(ppe_hook_parse("setcounter 258", &h), PPE_HOOK_SETCOUNTER);         CHECK_EQ(h.uarg, 258u);
  CHECK_EQ(ppe_hook_parse("setcounter 4294967280", &h), PPE_HOOK_SETCOUNTER);  CHECK(h.uarg == 0xFFFFFFF0u);
  CHECK_EQ(ppe_hook_parse("setcounter 4294967295", &h), PPE_HOOK_SETCOUNTER);  CHECK(h.uarg == 0xFFFFFFFFu);
  CHECK_EQ(ppe_hook_parse("setcounter 4294967296", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("setcounter 99999999999", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("setcounter -1", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("setcounter +5", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("setcounter 12x", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("setcounter", &h), PPE_HOOK_UNKNOWN);
}

PPE_TEST(s4_hook_skiptx_awake_and_batt_have_bounds, "TC-17") {
  ppe_hook_cmd_t h;
  CHECK_EQ(ppe_hook_parse("skiptx 0", &h), PPE_HOOK_SKIPTX);
  CHECK_EQ(ppe_hook_parse("skiptx 3", &h), PPE_HOOK_SKIPTX);    CHECK_EQ(h.arg, 3);
  CHECK_EQ(ppe_hook_parse("skiptx 100", &h), PPE_HOOK_SKIPTX);
  CHECK_EQ(ppe_hook_parse("skiptx 101", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("skiptx -1", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("awake 1", &h), PPE_HOOK_AWAKE);
  CHECK_EQ(ppe_hook_parse("awake 60", &h), PPE_HOOK_AWAKE);     CHECK_EQ(h.arg, 60);
  CHECK_EQ(ppe_hook_parse("awake 3600", &h), PPE_HOOK_AWAKE);
  CHECK_EQ(ppe_hook_parse("awake 0", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("awake 3601", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("batt 0", &h), PPE_HOOK_BATT_SET);
  CHECK_EQ(ppe_hook_parse("batt 3300", &h), PPE_HOOK_BATT_SET); CHECK_EQ(h.arg, 3300);
  CHECK_EQ(ppe_hook_parse("batt 5000", &h), PPE_HOOK_BATT_SET);
  CHECK_EQ(ppe_hook_parse("batt 5001", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("batt fail", &h), PPE_HOOK_BATT_FAIL);
  CHECK_EQ(ppe_hook_parse("batt real", &h), PPE_HOOK_BATT_REAL);
  CHECK_EQ(ppe_hook_parse("batt low", &h), PPE_HOOK_BAD_ARG);
}

PPE_TEST(s4_hook_fault_and_nvslog_words, "TC-17") {
  ppe_hook_cmd_t h;
  CHECK_EQ(ppe_hook_parse("fault nvs-write", &h), PPE_HOOK_FAULT_NVS_WRITE);
  CHECK_EQ(ppe_hook_parse("fault nvs-boot", &h), PPE_HOOK_FAULT_NVS_BOOT);
  CHECK_EQ(ppe_hook_parse("fault nvs-boot 1", &h), PPE_HOOK_UNKNOWN);
  CHECK_EQ(ppe_hook_parse("fault ble", &h), PPE_HOOK_FAULT_BLE);
  CHECK_EQ(ppe_hook_parse("fault clear", &h), PPE_HOOK_FAULT_CLEAR);
  CHECK_EQ(ppe_hook_parse("fault wdt", &h), PPE_HOOK_BAD_ARG);   // hub fault, not a node hook
  CHECK_EQ(ppe_hook_parse("fault", &h), PPE_HOOK_UNKNOWN);
  CHECK_EQ(ppe_hook_parse("nvslog on", &h), PPE_HOOK_NVSLOG_ON);
  CHECK_EQ(ppe_hook_parse("nvslog off", &h), PPE_HOOK_NVSLOG_OFF);
  CHECK_EQ(ppe_hook_parse("nvslog", &h), PPE_HOOK_UNKNOWN);
}

PPE_TEST(s4_hook_set_hb_uses_the_fr40_bounds, "TC-09") {
  ppe_hook_cmd_t h;
  CHECK_EQ(ppe_hook_parse("set hb 60", &h), PPE_HOOK_SET_HB);    CHECK_EQ(h.arg, 60);
  CHECK_EQ(ppe_hook_parse("set hb 360", &h), PPE_HOOK_SET_HB);
  CHECK_EQ(ppe_hook_parse("set hb 3600", &h), PPE_HOOK_SET_HB);
  CHECK_EQ(ppe_hook_parse("set hb 59", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("set hb 3601", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("set hb x", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("set hb", &h), PPE_HOOK_UNKNOWN);
  CHECK_EQ(ppe_hook_parse("set heartbeat 60", &h), PPE_HOOK_UNKNOWN);   // only the short word is a hook
}

PPE_TEST(s4_hook_ident_has_four_forms_and_checks_node_and_vest_ranges, "TC-01") {
  ppe_hook_cmd_t h;
  CHECK_EQ(ppe_hook_parse("ident set 1 1", &h), PPE_HOOK_IDENT_SET);    CHECK_EQ(h.arg, 1);  CHECK_EQ(h.arg2, 1);
  CHECK_EQ(ppe_hook_parse("ident set 5 254", &h), PPE_HOOK_IDENT_SET);  CHECK_EQ(h.arg, 5);  CHECK_EQ(h.arg2, 254);
  CHECK_EQ(ppe_hook_parse("ident set 0 1", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("ident set 6 1", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("ident set 1 0", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("ident set 1 255", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("ident set a b", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("ident set 1", &h), PPE_HOOK_UNKNOWN);
  CHECK_EQ(ppe_hook_parse("ident set 1 1 1", &h), PPE_HOOK_UNKNOWN);    // five tokens
  CHECK_EQ(ppe_hook_parse("ident testkey", &h), PPE_HOOK_IDENT_TESTKEY);
  CHECK_EQ(ppe_hook_parse("ident clear", &h), PPE_HOOK_IDENT_CLEAR);
  CHECK_EQ(ppe_hook_parse("ident show", &h), PPE_HOOK_IDENT_SHOW);
  CHECK_EQ(ppe_hook_parse("ident key", &h), PPE_HOOK_BAD_ARG);          // no hook takes key bytes on the command line
  CHECK_EQ(ppe_hook_parse("ident", &h), PPE_HOOK_UNKNOWN);
}

PPE_TEST(s4_hook_sleep_adcat_and_wakelog, "TC-14") {
  ppe_hook_cmd_t h;
  CHECK_EQ(ppe_hook_parse("sleep on", &h), PPE_HOOK_SLEEP_ON);
  CHECK_EQ(ppe_hook_parse("sleep off", &h), PPE_HOOK_SLEEP_OFF);
  CHECK_EQ(ppe_hook_parse("sleep now", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("sleep", &h), PPE_HOOK_UNKNOWN);
  CHECK_EQ(ppe_hook_parse("adcat 40 50", &h), PPE_HOOK_ADCAT);   CHECK_EQ(h.arg, 40);   CHECK_EQ(h.arg2, 50);
  CHECK_EQ(ppe_hook_parse("adcat 86400 4095", &h), PPE_HOOK_ADCAT);
  CHECK_EQ(ppe_hook_parse("adcat 1 0", &h), PPE_HOOK_ADCAT);
  CHECK_EQ(ppe_hook_parse("adcat 0 50", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("adcat 86401 50", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("adcat 40 4096", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("adcat 40 -1", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("adcat clear", &h), PPE_HOOK_ADCAT_CLEAR);
  CHECK_EQ(ppe_hook_parse("adcat 40", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("adcat", &h), PPE_HOOK_UNKNOWN);
  CHECK_EQ(ppe_hook_parse("wakelog", &h), PPE_HOOK_WAKELOG);
  CHECK_EQ(ppe_hook_parse("wakelog all", &h), PPE_HOOK_UNKNOWN);
}

PPE_TEST(s4_hook_unknown_commands_still_fall_through, "TC-78") {
  ppe_hook_cmd_t h;
  CHECK_EQ(ppe_hook_parse("reboot", &h), PPE_HOOK_UNKNOWN);
  CHECK_EQ(ppe_hook_parse("a b c d e", &h), PPE_HOOK_UNKNOWN);
  CHECK_EQ(ppe_hook_parse("show", &h), PPE_HOOK_SHOW);
  CHECK_EQ(ppe_hook_parse("adc 800", &h), PPE_HOOK_ADC_SET);
}

#endif
