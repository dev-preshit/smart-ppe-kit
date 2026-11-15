#include "ppe_sleep.h"
#include <string.h>

void ppe_sleep_state_init(ppe_sleep_state_t* s) { s->gpio_noop_wakes = 0; s->poll_mode = false; }

void ppe_sleep_note_wake(ppe_sleep_state_t* s, bool woke_by_gpio, bool candidate_changed_while_awake) {
  if (woke_by_gpio && !candidate_changed_while_awake) { if (s->gpio_noop_wakes < 255) s->gpio_noop_wakes++; }
  else s->gpio_noop_wakes = 0;
}

void ppe_sleep_note_abort(ppe_sleep_state_t* s) { if (s->gpio_noop_wakes < 255) s->gpio_noop_wakes++; }

static uint32_t min_u32(uint32_t a, uint32_t b) { return a < b ? a : b; }

// Is the sensor already at the wake level? Wake level = opposite of the confirmed state.
static bool at_wake_level(const ppe_sleep_in_t* in) {
  bool a = in->contact_a_closed, b = in->contact_b_closed;
  if (in->type == PPE_NTYPE_GLOVE) return in->confirmed == PPE_WEAR_WORN ? !a : a;
  // helmet: WORN needs both closed. Confirmed NOT_WORN: wake when any contact closes. Confirmed WORN: wake when any contact opens.
  return in->confirmed == PPE_WEAR_WORN ? !(a && b) : (a || b);
}

void ppe_sleep_decide(ppe_sleep_state_t* s, const ppe_sleep_in_t* in, ppe_sleep_out_t* out) {
  memset(out, 0, sizeof *out);
  if (in->node_fault) { out->mode = PPE_SLEEP_FOREVER; return; }             // V12-06
  if (in->type == PPE_NTYPE_FOOT) {                                           // A-18: no comparator, sample on a timer
    out->mode = PPE_SLEEP_POLL;
    out->timer_valid = true;
    out->timer_ms = in->hb_set ? min_u32(in->hb_remaining_ms, PPE_FSR_POLL_MS) : PPE_FSR_POLL_MS;
    return;
  }
  bool at_level = at_wake_level(in);
  if (s->poll_mode && !at_level) { s->poll_mode = false; s->gpio_noop_wakes = 0; }     // N-10b: the reading left the condition
  out->wake_on_closed = (in->confirmed != PPE_WEAR_WORN);
  out->pin_mask = (in->type == PPE_NTYPE_GLOVE) ? 0x01u : 0x03u;
  if (s->poll_mode || s->gpio_noop_wakes >= 2) {                             // N-10
    s->poll_mode = true;
    out->mode = PPE_SLEEP_POLL;
    out->timer_valid = true;
    out->timer_ms = in->hb_set ? min_u32(in->hb_remaining_ms, PPE_POLL_MS) : PPE_POLL_MS;
    return;
  }
  if (at_level) { out->mode = PPE_SLEEP_ABORT; return; }                     // N-07b
  out->mode = PPE_SLEEP_GPIO;                                                 // N-07
  out->timer_valid = in->hb_set;
  out->timer_ms = in->hb_set ? in->hb_remaining_ms : 0;
}
