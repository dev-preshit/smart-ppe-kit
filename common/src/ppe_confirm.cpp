#include "ppe_confirm.h"

void ppe_confirm_cold_start(ppe_confirm_t* c, uint32_t now_ms) {
  c->confirmed = ppe_raw_make(PPE_WEAR_NONE, 0);
  c->in_grace = true; c->grace_start = now_ms;          // N-20: boot grace starts now, no packet before expiry
  c->boot_pending = true; c->hb_set = false; c->hb_deadline = 0;
}

void ppe_confirm_resume(ppe_confirm_t* c, ppe_raw_t confirmed, bool boot_pending, bool hb_set, uint32_t hb_deadline) {
  c->confirmed = confirmed; c->in_grace = false; c->grace_start = 0;
  c->boot_pending = boot_pending; c->hb_set = hb_set; c->hb_deadline = hb_deadline;
}

void ppe_confirm_on_candidate(ppe_confirm_t* c, uint32_t now_ms, ppe_raw_t candidate) {
  if (c->in_grace) {
    if (c->confirmed.state == PPE_WEAR_NONE) c->grace_start = now_ms;        // N-23: restart boot grace
    else if (ppe_raw_eq(candidate, c->confirmed)) c->in_grace = false;       // N-22: returned, cancel, nothing sent
    // else: still differs from confirmed (e.g. a fault pair change): the timer keeps running, the difference is continuous
    return;
  }
  if (!ppe_raw_eq(candidate, c->confirmed)) { c->in_grace = true; c->grace_start = now_ms; }   // N-21
}

bool ppe_confirm_tick(ppe_confirm_t* c, uint32_t now_ms, ppe_raw_t candidate, uint16_t grace_s, ppe_event_t* ev) {
  if (!c->in_grace) return false;
  if ((uint32_t)(now_ms - c->grace_start) < (uint32_t)grace_s * 1000u) return false;
  c->confirmed = candidate; c->in_grace = false;                              // N-24
  if (ev) { ev->pair = candidate; ev->boot = c->boot_pending; }
  c->boot_pending = false;
  return true;
}

void ppe_hb_on_sequence_start(ppe_confirm_t* c, uint32_t t0_ms, uint16_t heartbeat_s) {
  c->hb_deadline = t0_ms + (uint32_t)heartbeat_s * 1000u; c->hb_set = true;
}

bool ppe_hb_due(const ppe_confirm_t* c, uint32_t now_ms) {
  if (!c->hb_set || c->confirmed.state == PPE_WEAR_NONE) return false;        // A-07: never while confirmed is NONE
  return (int32_t)(now_ms - c->hb_deadline) >= 0;
}
