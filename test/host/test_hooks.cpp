// S2 host tests: test-hook parser, MARK lines, CFG line. Compiled only with PPE_TEST_HOOKS (the host Makefile sets it).
#include "ppe_test.h"
#include "ppe_hooks.h"
#include <stdlib.h>

#ifdef PPE_TEST_HOOKS

PPE_TEST(hook_adc_accepts_0_to_4095_and_the_words_fail_ok_real, "TC-03") {
  ppe_hook_cmd_t h;
  CHECK_EQ(ppe_hook_parse("adc 0", &h), PPE_HOOK_ADC_SET);     CHECK_EQ(h.arg, 0);
  CHECK_EQ(ppe_hook_parse("adc 800", &h), PPE_HOOK_ADC_SET);   CHECK_EQ(h.arg, 800);
  CHECK_EQ(ppe_hook_parse("adc 4095", &h), PPE_HOOK_ADC_SET);  CHECK_EQ(h.arg, 4095);
  CHECK_EQ(ppe_hook_parse("adc 4096", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("adc -1", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("adc 12x", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("adc", &h), PPE_HOOK_UNKNOWN);
  CHECK_EQ(ppe_hook_parse("adc fail", &h), PPE_HOOK_ADC_FAIL);
  CHECK_EQ(ppe_hook_parse("adc ok", &h), PPE_HOOK_ADC_OK);
  CHECK_EQ(ppe_hook_parse("adc real", &h), PPE_HOOK_ADC_REAL);
}

PPE_TEST(hook_set_uses_the_fr40_bounds_for_settle_and_grace, "TC-05") {
  ppe_hook_cmd_t h;
  CHECK_EQ(ppe_hook_parse("set settle 0", &h), PPE_HOOK_SET_SETTLE);
  CHECK_EQ(ppe_hook_parse("set settle 500", &h), PPE_HOOK_SET_SETTLE);  CHECK_EQ(h.arg, 500);
  CHECK_EQ(ppe_hook_parse("set settle 1000", &h), PPE_HOOK_SET_SETTLE);
  CHECK_EQ(ppe_hook_parse("set settle 1001", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("set settle -1", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("set grace 1", &h), PPE_HOOK_SET_GRACE);
  CHECK_EQ(ppe_hook_parse("set grace 10", &h), PPE_HOOK_SET_GRACE);     CHECK_EQ(h.arg, 10);
  CHECK_EQ(ppe_hook_parse("set grace 120", &h), PPE_HOOK_SET_GRACE);
  CHECK_EQ(ppe_hook_parse("set grace 0", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("set grace 121", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("set grace ten", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("set grace", &h), PPE_HOOK_UNKNOWN);
  CHECK_EQ(ppe_hook_parse("set heartbeat 60", &h), PPE_HOOK_UNKNOWN);    // not a S2 hook
}

PPE_TEST(hook_parser_handles_whitespace_garbage_and_long_input, "TC-05") {
  ppe_hook_cmd_t h;
  CHECK_EQ(ppe_hook_parse("", &h), PPE_HOOK_EMPTY);
  CHECK_EQ(ppe_hook_parse("   \t ", &h), PPE_HOOK_EMPTY);
  CHECK_EQ(ppe_hook_parse(nullptr, &h), PPE_HOOK_EMPTY);
  CHECK_EQ(ppe_hook_parse("  show  ", &h), PPE_HOOK_SHOW);
  CHECK_EQ(ppe_hook_parse("show now", &h), PPE_HOOK_UNKNOWN);
  CHECK_EQ(ppe_hook_parse("marker on", &h), PPE_HOOK_MARKER_ON);
  CHECK_EQ(ppe_hook_parse("marker off", &h), PPE_HOOK_MARKER_OFF);
  CHECK_EQ(ppe_hook_parse("marker maybe", &h), PPE_HOOK_BAD_ARG);
  CHECK_EQ(ppe_hook_parse("tx", &h), PPE_HOOK_UNKNOWN);                  // later sprints
  CHECK_EQ(ppe_hook_parse("a b c d", &h), PPE_HOOK_UNKNOWN);
  CHECK_EQ(ppe_hook_parse("adcadcadcadcadcadcadc 1", &h), PPE_HOOK_UNKNOWN);
  CHECK_EQ(ppe_hook_parse("show", nullptr), PPE_HOOK_SHOW);              // out may be null
}

PPE_TEST(mark_lines_have_the_exact_format_the_timing_script_reads, "TC-06") {
  char b[64];
  ppe_mark_fmt(b, sizeof b, "GRACE_START", 5);
  CHECK(strcmp(b, "MARK GRACE_START 5") == 0);
  ppe_mark_fmt_raw(b, sizeof b, "CAND", 4294967295u, ppe_raw_make(PPE_WEAR_WORN, 0));
  CHECK(strcmp(b, "MARK CAND 4294967295 state=2 fault=0") == 0);
  ppe_mark_fmt_raw(b, sizeof b, "CONFIRM", 123456789u, ppe_raw_make(PPE_WEAR_NOT_WORN, 1));
  CHECK(strcmp(b, "MARK CONFIRM 123456789 state=1 fault=1") == 0);
  char small[10];
  ppe_mark_fmt_raw(small, sizeof small, "CONFIRM", 1u, ppe_raw_make(PPE_WEAR_WORN, 0));
  CHECK(strlen(small) < sizeof small);                                   // truncated but terminated
}

PPE_TEST(cfg_line_prints_the_parameters_and_nothing_secret, "TC-06") {
  ppe_cfg_t c; ppe_cfg_defaults(&c); char b[128];
  ppe_cfg_fmt(b, sizeof b, PPE_NODE_FOOT, &c);
  CHECK(strcmp(b, "CFG type=0 settle_ms=0 grace_s=25 heartbeat_s=360 n=16 hooks=1") == 0);
  ppe_cfg_set_grace(&c, 10); ppe_cfg_set_settle(&c, 500);
  ppe_cfg_fmt(b, sizeof b, PPE_NODE_HELMET, &c);
  CHECK(strcmp(b, "CFG type=2 settle_ms=500 grace_s=10 heartbeat_s=360 n=16 hooks=1") == 0);
}

#endif
