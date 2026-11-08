// S2 host tests: RTC context struct and the reset-reason guard (design only, no sleep behaviour). A-08.
#include "ppe_test.h"
#include "ppe_rtcctx.h"
#include "ppe_sense.h"

static ppe_rtc_fields_t good() {
  ppe_rtc_fields_t f;
  memset(&f, 0, sizeof f);
  f.counter = 5; f.ceiling = 64; f.hb_deadline = 123456; f.confirmed_state = PPE_WEAR_WORN; f.hb_set = 1; f.gpio_noop_wakes = 1;
  return f;
}

PPE_TEST(rtcctx_layout_is_32_bytes_and_crc32_matches_the_check_value, "TC-11") {
  CHECK_EQ(sizeof(ppe_rtcctx_t), 32);
  CHECK_EQ(ppe_crc32("123456789", 9), 0xCBF43926u);
  CHECK_EQ(ppe_crc32("", 0), 0);
}

PPE_TEST(rtcctx_round_trip_keeps_every_field, "TC-11") {
  ppe_rtc_fields_t f = good(), g; ppe_rtcctx_t x;
  f.sensor_fault = 1; f.boot_pending = 1; f.poll_mode = 1;
  ppe_rtcctx_pack(&x, &f);
  CHECK_EQ(ppe_rtcctx_unpack(&x, true, true, &g), PPE_RTC_OK);
  CHECK_EQ(g.counter, 5); CHECK_EQ(g.ceiling, 64); CHECK_EQ(g.hb_deadline, 123456);
  CHECK_EQ(g.confirmed_state, PPE_WEAR_WORN); CHECK_EQ(g.sensor_fault, 1); CHECK_EQ(g.boot_pending, 1);
  CHECK_EQ(g.gpio_noop_wakes, 1); CHECK_EQ(g.poll_mode, 1); CHECK_EQ(g.hb_set, 1);
}

PPE_TEST(rtcctx_refused_unless_deep_sleep_wake_and_valid_flag, "TC-11") {
  ppe_rtc_fields_t f = good(), g; ppe_rtcctx_t x; ppe_rtcctx_pack(&x, &f);
  CHECK_EQ(ppe_rtcctx_unpack(&x, false, true, &g), PPE_RTC_NOT_DEEP_SLEEP);    // power-on, brown-out, watchdog, software reset
  CHECK_EQ(ppe_rtcctx_unpack(&x, true, false, &g), PPE_RTC_FLAG_CLEAR);        // stale RAM image with a good CRC
  CHECK_EQ(ppe_rtcctx_unpack(&x, true, true, &g), PPE_RTC_OK);
}

PPE_TEST(rtcctx_every_single_bit_flip_is_rejected, "TC-11") {
  ppe_rtc_fields_t f = good(), g; ppe_rtcctx_t x; ppe_rtcctx_pack(&x, &f);
  for (size_t i = 0; i < sizeof x; i++)
    for (int b = 0; b < 8; b++) {
      ppe_rtcctx_t y = x; ((uint8_t*)&y)[i] ^= (uint8_t)(1u << b);
      CHECK(ppe_rtcctx_unpack(&y, true, true, &g) != PPE_RTC_OK);
    }
}

PPE_TEST(rtcctx_valid_looking_image_with_confirmed_none_is_invalid, "TC-11") {
  ppe_rtc_fields_t f = good(), g; ppe_rtcctx_t x;
  f.confirmed_state = PPE_WEAR_NONE; ppe_rtcctx_pack(&x, &f);
  CHECK_EQ(ppe_rtcctx_unpack(&x, true, true, &g), PPE_RTC_IMPLAUSIBLE);        // treated as COLD_START by the caller
}

PPE_TEST(rtcctx_implausible_fields_with_a_correct_crc_are_rejected, "TC-11") {
  ppe_rtc_fields_t g; ppe_rtcctx_t x;
  for (int k = 0; k < 9; k++) {
    ppe_rtc_fields_t f = good();
    switch (k) {
      case 0: f.counter = 100; f.ceiling = 64; break;                           // counter above the ceiling
      case 1: f.ceiling = 0xFFFFFFF1u; f.counter = 5; break;                    // ceiling above the limit
      case 2: f.counter = 0xFFFFFFF1u; f.ceiling = 0xFFFFFFF1u; break;
      case 3: f.sensor_fault = 2; break;
      case 4: f.boot_pending = 2; break;
      case 5: f.poll_mode = 2; break;
      case 6: f.hb_set = 2; break;
      case 7: f.confirmed_state = (ppe_wear_t)3; break;
      default: f.confirmed_state = (ppe_wear_t)255; break;
    }
    ppe_rtcctx_pack(&x, &f);
    CHECK_EQ(ppe_rtcctx_unpack(&x, true, true, &g), PPE_RTC_IMPLAUSIBLE);
  }
  ppe_rtc_fields_t edge = good(); edge.counter = 0xFFFFFFF0u; edge.ceiling = 0xFFFFFFF0u;   // the limit itself is allowed
  ppe_rtcctx_pack(&x, &edge);
  CHECK_EQ(ppe_rtcctx_unpack(&x, true, true, &g), PPE_RTC_OK);
}

PPE_TEST(rtcctx_zero_ones_random_and_null_images_are_rejected, "TC-11") {
  ppe_rtc_fields_t g; ppe_rtcctx_t x;
  memset(&x, 0, sizeof x);    CHECK(ppe_rtcctx_unpack(&x, true, true, &g) != PPE_RTC_OK);
  memset(&x, 0xFF, sizeof x); CHECK(ppe_rtcctx_unpack(&x, true, true, &g) != PPE_RTC_OK);
  CHECK(ppe_rtcctx_unpack(nullptr, true, true, &g) != PPE_RTC_OK);
  uint32_t r = 0x12345678u;
  for (int n = 0; n < 2000; n++) {
    uint8_t* p = (uint8_t*)&x;
    for (size_t i = 0; i < sizeof x; i++) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; p[i] = (uint8_t)r; }
    CHECK(ppe_rtcctx_unpack(&x, true, true, &g) != PPE_RTC_OK);
  }
}

PPE_TEST(rtcctx_wake_seeds_the_fsr_hysteresis_with_the_confirmed_state, "TC-11") {
  for (int p = 1; p <= 2; p++) {
    ppe_rtc_fields_t f = good(), g; ppe_rtcctx_t x;
    f.confirmed_state = (ppe_wear_t)p; ppe_rtcctx_pack(&x, &f);
    CHECK_EQ(ppe_rtcctx_unpack(&x, true, true, &g), PPE_RTC_OK);
    ppe_fsr_t fsr; ppe_fsr_init(&fsr); ppe_fsr_seed(&fsr, g.confirmed_state);
    CHECK_EQ(ppe_conv_fsr(&fsr, true, 800).state, p);                           // dead band holds the confirmed state
  }
}
