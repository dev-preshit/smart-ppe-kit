// S2 host tests: sensor conversion (state machines 1.3). A-04 mean/rounding, A-05 thresholds, A-06 fault range.
#include "ppe_test.h"
#include "ppe_sense.h"

PPE_TEST(glove_connected_is_worn_disconnected_is_not_worn, "TC-02") {
  for (int i = 0; i < 20; i++) {                         // full 20 cycles are free on the host
    ppe_raw_t w = ppe_conv_glove(true), n = ppe_conv_glove(false);
    CHECK_EQ(w.state, PPE_WEAR_WORN);     CHECK_EQ(w.fault, 0);
    CHECK_EQ(n.state, PPE_WEAR_NOT_WORN); CHECK_EQ(n.fault, 0);
  }
}

PPE_TEST(helmet_needs_both_factors_one_alone_is_not_worn, "TC-04") {
  for (int crown = 0; crown < 2; crown++)
    for (int strap = 0; strap < 2; strap++) {
      ppe_raw_t r = ppe_conv_helmet(crown != 0, strap != 0);
      CHECK_EQ(r.state, (crown && strap) ? PPE_WEAR_WORN : PPE_WEAR_NOT_WORN);
      CHECK_EQ(r.fault, 0);                              // sensor fault applies to FSR nodes only
    }
}

PPE_TEST(fsr_tc03_sequence_changes_state_only_at_1537_and_88, "TC-03") {
  const uint32_t seq[] = {0, 88, 89, 800, 1536, 1537, 4095, 1536, 89, 88};
  const ppe_wear_t want[] = {PPE_WEAR_NOT_WORN, PPE_WEAR_NOT_WORN, PPE_WEAR_NOT_WORN, PPE_WEAR_NOT_WORN, PPE_WEAR_NOT_WORN,
                             PPE_WEAR_WORN, PPE_WEAR_WORN, PPE_WEAR_WORN, PPE_WEAR_WORN, PPE_WEAR_NOT_WORN};
  ppe_fsr_t f; ppe_fsr_init(&f);
  for (int i = 0; i < 10; i++) {
    ppe_raw_t r = ppe_conv_fsr(&f, true, seq[i]);
    CHECK_EQ(r.state, want[i]); CHECK_EQ(r.fault, 0);
  }
}

PPE_TEST(fsr_power_on_in_dead_band_is_not_worn_then_holds, "TC-03") {
  for (uint32_t c = 89; c <= 1536; c++) {                // R-5: no history gives NOT_WORN
    ppe_fsr_t f; ppe_fsr_init(&f);
    CHECK_EQ(ppe_conv_fsr(&f, true, c).state, PPE_WEAR_NOT_WORN);
  }
  ppe_fsr_t f; ppe_fsr_init(&f);
  CHECK_EQ(ppe_conv_fsr(&f, true, 800).state, PPE_WEAR_NOT_WORN);
  CHECK_EQ(ppe_conv_fsr(&f, true, 1537).state, PPE_WEAR_WORN);
}

PPE_TEST(fsr_every_count_follows_the_rule_from_both_previous_states, "TC-03") {
  for (int p = 1; p <= 2; p++)
    for (uint32_t c = 0; c <= 4095; c++) {
      ppe_fsr_t f; ppe_fsr_init(&f); ppe_fsr_seed(&f, (ppe_wear_t)p);
      ppe_wear_t want = c >= 1537 ? PPE_WEAR_WORN : (c <= 88 ? PPE_WEAR_NOT_WORN : (ppe_wear_t)p);
      CHECK_EQ(ppe_conv_fsr(&f, true, c).state, want);
    }
}

PPE_TEST(fsr_mean_rounds_to_nearest_so_88_point_4_counts_as_88, "TC-03") {
  uint16_t s[16];
  for (int i = 0; i < 16; i++) s[i] = 88;
  CHECK_EQ(ppe_fsr_mean(s, 16), 88);
  s[0] = 94;  CHECK_EQ(ppe_fsr_mean(s, 16), 88);         // 88.375
  s[0] = 95;  CHECK_EQ(ppe_fsr_mean(s, 16), 88);         // 88.4375
  s[0] = 96;  CHECK_EQ(ppe_fsr_mean(s, 16), 89);         // 88.5 rounds up, so 89 holds the previous state
  for (int i = 0; i < 16; i++) s[i] = 4095;
  CHECK_EQ(ppe_fsr_mean(s, 16), 4095);                   // no overflow at full scale
  CHECK_EQ(ppe_fsr_mean(s, 0), 0);
  CHECK_EQ(ppe_fsr_mean(nullptr, 16), 0);
}

PPE_TEST(fsr_seed_after_wake_uses_the_confirmed_state, "TC-03") {
  ppe_fsr_t f;
  ppe_fsr_init(&f); ppe_fsr_seed(&f, PPE_WEAR_WORN);
  CHECK_EQ(ppe_conv_fsr(&f, true, 800).state, PPE_WEAR_WORN);
  ppe_fsr_init(&f); ppe_fsr_seed(&f, PPE_WEAR_NOT_WORN);
  CHECK_EQ(ppe_conv_fsr(&f, true, 800).state, PPE_WEAR_NOT_WORN);
  ppe_fsr_init(&f); ppe_fsr_seed(&f, PPE_WEAR_NONE);     // NONE is not a usable history
  CHECK_EQ(ppe_conv_fsr(&f, true, 800).state, PPE_WEAR_NOT_WORN);
}

PPE_TEST(fsr_read_error_or_out_of_range_is_not_worn_with_fault, "TC-16") {
  ppe_fsr_t f; ppe_fsr_init(&f);
  ppe_conv_fsr(&f, true, 3000);
  ppe_raw_t r = ppe_conv_fsr(&f, false, 3000);           // read error or timeout, the count is ignored
  CHECK_EQ(r.state, PPE_WEAR_NOT_WORN); CHECK_EQ(r.fault, 1);
  r = ppe_conv_fsr(&f, true, 4096);                      // outside 0..4095
  CHECK_EQ(r.state, PPE_WEAR_NOT_WORN); CHECK_EQ(r.fault, 1);
  r = ppe_conv_fsr(&f, true, 4095);                      // full scale is a valid reading (A-06)
  CHECK_EQ(r.state, PPE_WEAR_WORN); CHECK_EQ(r.fault, 0);
  r = ppe_conv_fsr(&f, true, 0);
  CHECK_EQ(r.state, PPE_WEAR_NOT_WORN); CHECK_EQ(r.fault, 0);
}

PPE_TEST(fsr_fault_clears_with_next_valid_reading_and_dead_band_reads_not_worn, "TC-16") {
  ppe_fsr_t f; ppe_fsr_init(&f);
  ppe_conv_fsr(&f, true, 2500);                          // WORN
  ppe_conv_fsr(&f, false, 0);                            // fault
  ppe_raw_t r = ppe_conv_fsr(&f, true, 800);             // valid again, in the dead band
  CHECK_EQ(r.fault, 0);
  CHECK_EQ(r.state, PPE_WEAR_NOT_WORN);                  // the fault set the memory to NOT_WORN (fail-safe)
  r = ppe_conv_fsr(&f, true, 2500);
  CHECK_EQ(r.state, PPE_WEAR_WORN); CHECK_EQ(r.fault, 0);
}
