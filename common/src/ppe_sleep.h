// ppe_sleep.h - sleep decisions (state machines N-06, N-07, N-07b, N-10, N-10b and the NODE_FAULT rule V12-06; SRS FR-14, FR-15).
// Pure logic: no timers, no GPIO, no radio. The firmware reads the sensor, calls these two functions and arms what they say.
//   FOOT (FSR) nodes have no wake comparator (A-18): they wake on a timer every PPE_FSR_POLL_MS and sample the ADC (AS-04).
//   Glove and helmet nodes wake on a level on the contact pins, plus a timer for the next heartbeat. After two wakes that
//   changed nothing, the node switches to a timer-poll wake (period PPE_POLL_MS) until the reading leaves the condition.
#pragma once
#include <stdint.h>
#include "ppe_sense.h"

#ifndef PPE_POLL_MS
#define PPE_POLL_MS 1000u            // timer-poll period (FR-15: at most 1 s, set in S2)
#endif
#ifndef PPE_FSR_POLL_MS
#define PPE_FSR_POLL_MS 1000u        // FOOT node: ADC sampling period while asleep (A-18); power cost noted in CHANGES
#endif

typedef struct {
  uint8_t gpio_noop_wakes;           // consecutive GPIO wakes that changed nothing (kept in RTC memory)
  bool    poll_mode;                 // timer-poll instead of the GPIO wake (kept in RTC memory)
} ppe_sleep_state_t;

typedef enum {
  PPE_SLEEP_ABORT = 0,               // N-07b: the sensor is already at the wake level, do not sleep
  PPE_SLEEP_GPIO,                    // N-07: arm the GPIO wake (and the heartbeat timer)
  PPE_SLEEP_POLL,                    // N-10 or FOOT: timer wake only
  PPE_SLEEP_FOREVER                  // NODE_FAULT: no wake source at all until a reset
} ppe_sleep_mode_t;

typedef struct {
  ppe_node_type_t type;
  ppe_wear_t confirmed;              // NOT_WORN or WORN
  bool contact_a_closed, contact_b_closed;
  bool hb_set;
  uint32_t hb_remaining_ms;          // time to the next heartbeat (0 = due now)
  bool node_fault;
} ppe_sleep_in_t;

typedef struct {
  ppe_sleep_mode_t mode;
  bool     wake_on_closed;           // GPIO: wake when a contact CLOSES (confirmed NOT_WORN) or OPENS (confirmed WORN)
  uint8_t  pin_mask;                 // bit 0 = contact A, bit 1 = contact B
  bool     timer_valid;
  uint32_t timer_ms;                 // timer wake (heartbeat, poll period or both: the smaller)
} ppe_sleep_out_t;

void ppe_sleep_state_init(ppe_sleep_state_t* s);
// N-06: call ONCE per wake, when the node first becomes idle. A GPIO wake that changed nothing counts as a no-op wake;
// anything else clears the count.
void ppe_sleep_note_wake(ppe_sleep_state_t* s, bool woke_by_gpio, bool candidate_changed_while_awake);
// The sleep attempt was aborted because the sensor was already at the wake level (N-07b): that is a wake that changed nothing.
void ppe_sleep_note_abort(ppe_sleep_state_t* s);
// N-07 / N-07b / N-10 / N-10b: decide how the node goes to sleep. May change *s (poll_mode on or off).
void ppe_sleep_decide(ppe_sleep_state_t* s, const ppe_sleep_in_t* in, ppe_sleep_out_t* out);
