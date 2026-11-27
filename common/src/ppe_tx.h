// ppe_tx.h - transmit sequencer (state machines 1.7: T-01, T-03 to T-06, N-26 to N-28). Pure logic: injected ms clock (wrap-safe),
// no radio, no heap, no NVS. The caller builds the packet (counter, flags, HMAC, T-02 block extension, T-07 start retry),
// starts and stops the radio and tells this module what it did. Sequences never interleave; EVENT has priority over HEARTBEAT;
// a HEARTBEAT request made while a sequence runs waits for its end. Copy k starts at t0 + k * retx_ms (T-05).
#pragma once
#include <stdint.h>
#include "ppe_proto.h"
#include "ppe_confirm.h"

typedef enum { PPE_TX_IDLE = 0, PPE_TX_BUILD, PPE_TX_COPY, PPE_TX_GAP } ppe_tx_state_t;
typedef enum {
  PPE_TX_ACT_NONE = 0,
  PPE_TX_ACT_BUILD_EVENT,      // build the packet for t->cur_event, then call ppe_tx_begin() (or ppe_tx_abort() on a fault)
  PPE_TX_ACT_BUILD_HEARTBEAT,  // same, with the confirmed pair
  PPE_TX_ACT_START_COPY,       // start advertising the same 21 bytes (copy number t->copy)
  PPE_TX_ACT_STOP_COPY         // stop advertising
} ppe_tx_act_t;

#define PPE_TX_EVENT_QUEUE 2   // logical events that can wait while a sequence runs

typedef struct {
  ppe_tx_state_t state;
  uint8_t  copy;               // 0..PPE_RETX_COUNT-1: the copy running (COPY) or last started (GAP)
  uint32_t t0;                 // sequence start
  uint32_t copy_start;         // start of the running copy
  bool     hb_req;
  uint8_t  ev_n;
  ppe_event_t ev_q[PPE_TX_EVENT_QUEUE];
  ppe_event_t cur_event;       // valid after PPE_TX_ACT_BUILD_EVENT
} ppe_tx_t;

void ppe_tx_init(ppe_tx_t* t);
// False = the queue is full (the request is dropped and the caller logs it).
bool ppe_tx_request_event(ppe_tx_t* t, ppe_event_t ev);
// False = a heartbeat is already requested (N-26: no second request).
bool ppe_tx_request_heartbeat(ppe_tx_t* t);
// Call every loop pass. burst_ms = ADV_BURST_MS, retx_ms = 500. Returns what the caller must do now.
ppe_tx_act_t ppe_tx_step(ppe_tx_t* t, uint32_t now_ms, uint16_t burst_ms, uint16_t retx_ms);
// T-03: the packet is built. Copy 0 starts now (the caller starts the radio), t0 = now_ms. No-op unless state is BUILD.
void ppe_tx_begin(ppe_tx_t* t, uint32_t now_ms);
// Faults (N-30, N-31): drop the running sequence and every pending request.
void ppe_tx_abort(ppe_tx_t* t);
static inline bool ppe_tx_busy(const ppe_tx_t* t) { return t->state != PPE_TX_IDLE; }
