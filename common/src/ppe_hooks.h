// ppe_hooks.h - test-hook command parser and MARK line formatter. Compiled ONLY with -DPPE_TEST_HOOKS,
// so a release build contains none of it (TC-78). Pure functions: host-testable.
#pragma once
#ifdef PPE_TEST_HOOKS
#include <stddef.h>
#include <stdint.h>
#include "ppe_sense.h"
#include "ppe_config.h"

typedef enum {
  PPE_HOOK_EMPTY = 0, PPE_HOOK_ADC_SET, PPE_HOOK_ADC_FAIL, PPE_HOOK_ADC_OK, PPE_HOOK_ADC_REAL,
  PPE_HOOK_MARKER_ON, PPE_HOOK_MARKER_OFF, PPE_HOOK_SET_SETTLE, PPE_HOOK_SET_GRACE, PPE_HOOK_SHOW,
  // S4
  PPE_HOOK_TX_HB, PPE_HOOK_TX_EVENT, PPE_HOOK_SETCOUNTER, PPE_HOOK_SKIPTX, PPE_HOOK_NVSLOG_ON, PPE_HOOK_NVSLOG_OFF,
  PPE_HOOK_FAULT_NVS_WRITE, PPE_HOOK_FAULT_BLE, PPE_HOOK_FAULT_CLEAR, PPE_HOOK_AWAKE,
  PPE_HOOK_BATT_SET, PPE_HOOK_BATT_FAIL, PPE_HOOK_BATT_REAL, PPE_HOOK_SET_HB,
  PPE_HOOK_IDENT_SET, PPE_HOOK_IDENT_TESTKEY, PPE_HOOK_IDENT_CLEAR, PPE_HOOK_IDENT_SHOW,
  PPE_HOOK_UNKNOWN, PPE_HOOK_BAD_ARG
} ppe_hook_t;
// arg: number; arg2: second number (ident set <node> <vest>); uarg: 32-bit value (setcounter). tx event: arg = 1 worn, 0 not worn.
typedef struct { ppe_hook_t cmd; long arg; long arg2; uint32_t uarg; } ppe_hook_cmd_t;

// S2: adc <0..4095> | adc fail | adc ok | adc real | marker on|off | set settle <0..1000> | set grace <1..120> | show
// S4: tx hb | tx worn | tx notworn | setcounter <0..4294967295> | skiptx <0..100> | nvslog on|off | fault nvs-write|ble|clear |
//     awake <1..3600> | batt <0..5000 mV> | batt fail | batt real | set hb <60..3600> |
//     ident set <node 1..5> <vest 1..254> | ident testkey | ident clear | ident show
ppe_hook_t ppe_hook_parse(const char* line, ppe_hook_cmd_t* out);

// "MARK <NAME> <micros>" and "MARK <NAME> <micros> state=<n> fault=<n>". Returns snprintf's length.
int ppe_mark_fmt(char* buf, size_t n, const char* name, uint32_t micros_us);
int ppe_mark_fmt_raw(char* buf, size_t n, const char* name, uint32_t micros_us, ppe_raw_t raw);
// "CFG type=<n> settle_ms=<n> grace_s=<n> heartbeat_s=<n> n=<samples> hooks=1" (no key, there is none yet)
int ppe_cfg_fmt(char* buf, size_t n, ppe_node_type_t type, const ppe_cfg_t* c);
#endif
