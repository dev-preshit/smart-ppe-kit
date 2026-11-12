#include "ppe_tx.h"
#include <string.h>

static inline bool reached(uint32_t now, uint32_t target) { return (int32_t)(now - target) >= 0; }

void ppe_tx_init(ppe_tx_t* t) { memset(t, 0, sizeof *t); t->state = PPE_TX_IDLE; }

bool ppe_tx_request_event(ppe_tx_t* t, ppe_event_t ev) {
  if (t->ev_n >= PPE_TX_EVENT_QUEUE) return false;
  t->ev_q[t->ev_n++] = ev;
  return true;
}

bool ppe_tx_request_heartbeat(ppe_tx_t* t) {
  if (t->hb_req) return false;
  t->hb_req = true;
  return true;
}

ppe_tx_act_t ppe_tx_step(ppe_tx_t* t, uint32_t now_ms, uint16_t burst_ms, uint16_t retx_ms) {
  switch (t->state) {
    case PPE_TX_IDLE:
      if (t->ev_n > 0) {                              // T-01, N-28: EVENT first, the heartbeat request is discarded
        t->cur_event = t->ev_q[0];
        for (uint8_t i = 1; i < t->ev_n; i++) t->ev_q[i - 1] = t->ev_q[i];
        t->ev_n--;
        t->hb_req = false;
        t->state = PPE_TX_BUILD;
        return PPE_TX_ACT_BUILD_EVENT;
      }
      if (t->hb_req) {
        t->hb_req = false;
        t->state = PPE_TX_BUILD;
        return PPE_TX_ACT_BUILD_HEARTBEAT;
      }
      return PPE_TX_ACT_NONE;
    case PPE_TX_BUILD:                                // waiting for ppe_tx_begin() or ppe_tx_abort()
      return PPE_TX_ACT_NONE;
    case PPE_TX_COPY:
      if (reached(now_ms, t->copy_start + burst_ms)) {                 // T-04
        t->state = (t->copy + 1u >= PPE_RETX_COUNT) ? PPE_TX_IDLE : PPE_TX_GAP;   // T-06 after the last copy
        return PPE_TX_ACT_STOP_COPY;
      }
      return PPE_TX_ACT_NONE;
    case PPE_TX_GAP:
      if (reached(now_ms, t->t0 + (uint32_t)(t->copy + 1u) * retx_ms)) {   // T-05: scheduled from t0, no drift
        t->copy++;
        t->copy_start = now_ms;
        t->state = PPE_TX_COPY;
        return PPE_TX_ACT_START_COPY;
      }
      return PPE_TX_ACT_NONE;
  }
  return PPE_TX_ACT_NONE;
}

void ppe_tx_begin(ppe_tx_t* t, uint32_t now_ms) {
  if (t->state != PPE_TX_BUILD) return;
  t->t0 = now_ms;
  t->copy = 0;
  t->copy_start = now_ms;
  t->state = PPE_TX_COPY;
}

void ppe_tx_abort(ppe_tx_t* t) {
  t->state = PPE_TX_IDLE;
  t->hb_req = false;
  t->ev_n = 0;
  t->copy = 0;
}
