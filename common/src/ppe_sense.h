// ppe_sense.h - node sensor conversion (state machines 1.3, R-1..R-7). Pure logic, no hardware access.
// A-04: FSR filtered count = mean of PPE_FSR_SAMPLES samples inside one poll, rounded, then compared.
// A-05: thresholds are compile-time constants (1537 / 88).  A-06: plausible range is the full 0..4095.
#pragma once
#include <stdint.h>

#ifndef PPE_FSR_WORN_MIN
#define PPE_FSR_WORN_MIN 1537       // count >= this: WORN
#endif
#ifndef PPE_FSR_NOT_WORN_MAX
#define PPE_FSR_NOT_WORN_MAX 88     // count <= this: NOT_WORN
#endif
#ifndef PPE_FSR_SAMPLES
#define PPE_FSR_SAMPLES 16          // samples per poll, one ADC burst
#endif
#define PPE_ADC_MAX 4095

typedef enum { PPE_WEAR_NONE = 0, PPE_WEAR_NOT_WORN = 1, PPE_WEAR_WORN = 2 } ppe_wear_t;
typedef enum { PPE_NODE_FOOT = 0, PPE_NODE_GLOVE = 1, PPE_NODE_HELMET = 2 } ppe_node_type_t;
typedef struct { ppe_wear_t state; uint8_t fault; } ppe_raw_t;      // the monitored pair

static inline bool ppe_raw_eq(ppe_raw_t a, ppe_raw_t b) { return a.state == b.state && a.fault == b.fault; }
static inline ppe_raw_t ppe_raw_make(ppe_wear_t s, uint8_t f) { ppe_raw_t r; r.state = s; r.fault = f; return r; }

ppe_raw_t ppe_conv_glove(bool connected);                                   // R-1
ppe_raw_t ppe_conv_helmet(bool crown_closed, bool strap_closed);            // R-3
uint16_t  ppe_fsr_mean(const uint16_t* samples, unsigned n);                // rounded mean, 0 if n == 0

typedef struct { bool have_prev; ppe_wear_t prev; } ppe_fsr_t;              // hysteresis memory (previous raw state)
void ppe_fsr_init(ppe_fsr_t* f);                                            // cold start: no history (R-5)
void ppe_fsr_seed(ppe_fsr_t* f, ppe_wear_t prev);                           // after a deep-sleep wake: prev = confirmed (A-08)
// adc_ok = false means read error or timeout. count > 4095 is outside the plausible range and is also a fault.
// A fault reports (NOT_WORN, 1) and sets the memory to NOT_WORN (literal "previous raw state"), R-4/R-6.
ppe_raw_t ppe_conv_fsr(ppe_fsr_t* f, bool adc_ok, uint32_t count);          // R-2, R-4, R-5, R-6
