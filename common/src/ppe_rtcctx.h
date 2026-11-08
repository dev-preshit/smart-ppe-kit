// ppe_rtcctx.h - context kept across deep sleep (state machines 1.1, SRS D-16). Design and host test only in S2:
// no sleep behaviour here. The firmware keeps the struct in RTC memory, an RTC-valid flag next to it, and passes
// the reset reason in (resume() of the counter cannot detect a stale image by itself).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "ppe_sense.h"

#define PPE_RTC_MAGIC   0x32455050u     // "PPE2" little endian
#define PPE_RTC_VERSION 1
#define PPE_RTC_COUNTER_LIMIT 0xFFFFFFF0u

typedef struct {                        // 32 bytes, fixed layout, no padding
  uint32_t magic;
  uint16_t version;
  uint16_t length;
  uint32_t counter;
  uint32_t ceiling;
  uint32_t hb_deadline;
  uint8_t  confirmed_state;             // ppe_wear_t, NOT_WORN or WORN only
  uint8_t  sensor_fault;
  uint8_t  boot_pending;
  uint8_t  gpio_noop_wakes;
  uint8_t  poll_mode;
  uint8_t  hb_set;
  uint8_t  reserved[2];
  uint32_t crc;                         // CRC-32 over every byte before this field
} ppe_rtcctx_t;
static_assert(sizeof(ppe_rtcctx_t) == 32, "ppe_rtcctx_t layout changed");

typedef struct {
  uint32_t counter, ceiling, hb_deadline;
  ppe_wear_t confirmed_state; uint8_t sensor_fault, boot_pending, gpio_noop_wakes, poll_mode, hb_set;
} ppe_rtc_fields_t;

typedef enum {
  PPE_RTC_OK = 0, PPE_RTC_NOT_DEEP_SLEEP, PPE_RTC_FLAG_CLEAR, PPE_RTC_BAD_MAGIC, PPE_RTC_BAD_VERSION,
  PPE_RTC_BAD_CRC, PPE_RTC_IMPLAUSIBLE
} ppe_rtc_result_t;

uint32_t ppe_crc32(const void* data, size_t n);
void ppe_rtcctx_pack(ppe_rtcctx_t* out, const ppe_rtc_fields_t* f);
// Valid only when: reset reason is deep-sleep wake AND the caller's RTC-valid flag is set AND magic, version,
// length and CRC match AND the fields are plausible. A valid-looking image with confirmed NONE is invalid (A-08).
ppe_rtc_result_t ppe_rtcctx_unpack(const ppe_rtcctx_t* in, bool reset_is_deep_sleep_wake, bool rtc_valid_flag,
                                   ppe_rtc_fields_t* out);
