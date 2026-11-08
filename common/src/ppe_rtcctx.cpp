#include "ppe_rtcctx.h"
#include <string.h>

uint32_t ppe_crc32(const void* data, size_t n) {
  const uint8_t* p = (const uint8_t*)data; uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) {
    crc ^= p[i];
    for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

void ppe_rtcctx_pack(ppe_rtcctx_t* o, const ppe_rtc_fields_t* f) {
  memset(o, 0, sizeof(*o));
  o->magic = PPE_RTC_MAGIC; o->version = PPE_RTC_VERSION; o->length = (uint16_t)sizeof(*o);
  o->counter = f->counter; o->ceiling = f->ceiling; o->hb_deadline = f->hb_deadline;
  o->confirmed_state = (uint8_t)f->confirmed_state; o->sensor_fault = f->sensor_fault;
  o->boot_pending = f->boot_pending; o->gpio_noop_wakes = f->gpio_noop_wakes; o->poll_mode = f->poll_mode;
  o->hb_set = f->hb_set;
  o->crc = ppe_crc32(o, offsetof(ppe_rtcctx_t, crc));
}

ppe_rtc_result_t ppe_rtcctx_unpack(const ppe_rtcctx_t* in, bool deep_sleep_wake, bool flag, ppe_rtc_fields_t* out) {
  if (!in) return PPE_RTC_BAD_MAGIC;
  if (!deep_sleep_wake) return PPE_RTC_NOT_DEEP_SLEEP;
  if (!flag) return PPE_RTC_FLAG_CLEAR;
  if (in->magic != PPE_RTC_MAGIC) return PPE_RTC_BAD_MAGIC;
  if (in->version != PPE_RTC_VERSION || in->length != sizeof(*in)) return PPE_RTC_BAD_VERSION;
  if (in->crc != ppe_crc32(in, offsetof(ppe_rtcctx_t, crc))) return PPE_RTC_BAD_CRC;
  if (in->confirmed_state != PPE_WEAR_NOT_WORN && in->confirmed_state != PPE_WEAR_WORN) return PPE_RTC_IMPLAUSIBLE;
  if (in->sensor_fault > 1 || in->boot_pending > 1 || in->poll_mode > 1 || in->hb_set > 1) return PPE_RTC_IMPLAUSIBLE;
  if (in->ceiling > PPE_RTC_COUNTER_LIMIT || in->counter > in->ceiling) return PPE_RTC_IMPLAUSIBLE;
  if (out) {
    out->counter = in->counter; out->ceiling = in->ceiling; out->hb_deadline = in->hb_deadline;
    out->confirmed_state = (ppe_wear_t)in->confirmed_state; out->sensor_fault = in->sensor_fault;
    out->boot_pending = in->boot_pending; out->gpio_noop_wakes = in->gpio_noop_wakes;
    out->poll_mode = in->poll_mode; out->hb_set = in->hb_set;
  }
  return PPE_RTC_OK;
}
