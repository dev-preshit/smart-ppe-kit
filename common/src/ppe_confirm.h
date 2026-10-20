// ppe_confirm.h - confirm and grace (state machines 1.5, N-20..N-24) plus the A-07 heartbeat guard.
// Injected clock (ms, wrap-safe). No transmit here: an EVENT is only requested (returned to the caller).
#pragma once
#include <stdint.h>
#include "ppe_sense.h"

typedef struct {
  ppe_raw_t confirmed;        // state NONE after a cold start
  bool      in_grace;
  uint32_t  grace_start;
  bool      boot_pending;     // the next EVENT carries BOOT=1
  bool      hb_set;           // hb_deadline valid (A-07: only after the first sequence starts)
  uint32_t  hb_deadline;
} ppe_confirm_t;

typedef struct { ppe_raw_t pair; bool boot; } ppe_event_t;

void ppe_confirm_cold_start(ppe_confirm_t* c, uint32_t now_ms);                                   // N-04, N-20
void ppe_confirm_resume(ppe_confirm_t* c, ppe_raw_t confirmed, bool boot_pending, bool hb_set, uint32_t hb_deadline);  // N-03
// Call when ppe_settle_step returned true (CANDIDATE_CHANGED): N-21, N-22, N-23.
void ppe_confirm_on_candidate(ppe_confirm_t* c, uint32_t now_ms, ppe_raw_t candidate);
// Call every poll. True when grace expired: confirmed = candidate and *ev is the EVENT to request (N-24).
bool ppe_confirm_tick(ppe_confirm_t* c, uint32_t now_ms, ppe_raw_t candidate, uint16_t grace_s, ppe_event_t* ev);

void ppe_hb_on_sequence_start(ppe_confirm_t* c, uint32_t t0_ms, uint16_t heartbeat_s);           // N-27
bool ppe_hb_due(const ppe_confirm_t* c, uint32_t now_ms);                                         // N-26 with the A-07 guard
