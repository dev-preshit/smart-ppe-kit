// Bench test hooks (serial commands). Compiled ONLY with -DPPE_TEST_HOOKS; the release build contains none
// of this code or its strings (checked by tools/build_check.py and the host Makefile release_check target).
#pragma once
#ifdef PPE_TEST_HOOKS
#include <stddef.h>
#include <stdint.h>
#include "hub_config.h"

enum HubHookType {
  HUB_HOOK_NONE = 0,     // empty line
  HUB_HOOK_ERROR,        // unknown command or bad argument
  HUB_HOOK_INJECT,       // inject <hex>      -> same rx queue and task as a BLE advertisement
  HUB_HOOK_STALL,        // stall <n>         -> stop the scan; the next n restart attempts fail
  HUB_HOOK_DUMP,         // dump              -> registry, counters, queue fill, timing
  HUB_HOOK_TIME_NOW,     // time              -> print the monotonic time once
  HUB_HOOK_TIME_ON,      // time on|off       -> time stamp on every accepted packet (default on)
  HUB_HOOK_TIME_OFF,
  HUB_HOOK_RXLOG_ON,     // rxlog on|off      -> every received advertisement before filtering (default off)
  HUB_HOOK_RXLOG_OFF,
  HUB_HOOK_SET_MARGIN,   // set margin <s>    -> OFFLINE_MARGIN_S (bounds are checked by the pipeline, 0..300)
  HUB_HOOK_SET_HEARTBEAT, // set heartbeat <s> -> HEARTBEAT_S for the hub (bounds are checked by the pipeline, 60..3600)
  HUB_HOOK_MOTOR,        // motor            -> one MOTOR status line (state, output, guard trips, longest ON)
  HUB_HOOK_MARKER_ON,    // marker on|off    -> pulse the marker pin (1 ms) at every accepted packet (edge recorder channel B)
  HUB_HOOK_MARKER_OFF,
  HUB_HOOK_PULSE         // pulse <on_ms> <off_ms> <count> -> known pulse train on the marker pin (recorder calibration)
};

struct HubHookCmd {
  HubHookType type;
  uint8_t data[HUB_RX_ADV_MAX];   // inject payload
  uint8_t len;                    // inject length (0 allowed: the zero-length defect)
  uint8_t n;                      // stall count 1..9
  uint32_t val, val2, val3;       // set margin / set heartbeat value; pulse on_ms, off_ms, count (digits only, up to 6 each)
};

HubHookCmd hub_hook_parse(const char* line);
#endif
