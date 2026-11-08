#include "ppe_settle.h"

void ppe_settle_init(ppe_settle_t* s, ppe_raw_t initial) { s->candidate = initial; s->pending = false; s->t_start = 0; }

bool ppe_settle_step(ppe_settle_t* s, uint32_t now_ms, ppe_raw_t raw, uint16_t settle_ms) {
  if (ppe_raw_eq(raw, s->candidate)) { s->pending = false; return false; }      // N-13
  if (settle_ms == 0) { s->candidate = raw; s->pending = false; return true; }  // N-12
  if (!s->pending) { s->pending = true; s->t_start = now_ms; return false; }    // N-11
  if ((uint32_t)(now_ms - s->t_start) >= settle_ms) {                           // N-14
    s->candidate = raw; s->pending = false; return true;
  }
  return false;
}
