// ppe_settle.h - settle filter (state machines 1.4, N-11..N-14). Injected clock, no delay().
#pragma once
#include <stdint.h>
#include "ppe_sense.h"

typedef struct { ppe_raw_t candidate; bool pending; uint32_t t_start; } ppe_settle_t;

// Cold start: candidate = first raw reading, no settle applied (N-04). Resume: candidate = confirmed.
void ppe_settle_init(ppe_settle_t* s, ppe_raw_t initial);
// Call every 50 ms poll. Returns true when the candidate changed (CANDIDATE_CHANGED).
bool ppe_settle_step(ppe_settle_t* s, uint32_t now_ms, ppe_raw_t raw, uint16_t settle_ms);
