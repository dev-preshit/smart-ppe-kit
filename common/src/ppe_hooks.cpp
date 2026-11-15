#include "ppe_hooks.h"
#ifdef PPE_TEST_HOOKS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Splits a line into at most 4 whitespace separated tokens. Returns the token count, or -1 if there are more.
static int tokens(const char* line, char t[4][16]) {
  int n = 0;
  while (*line) {
    while (*line && isspace((unsigned char)*line)) line++;
    if (!*line) break;
    if (n == 4) return -1;
    size_t k = 0;
    while (*line && !isspace((unsigned char)*line)) { if (k < 15) t[n][k] = *line; k++; line++; }
    if (k > 15) return -1;
    t[n][k] = 0; n++;
  }
  return n;
}

static bool to_long(const char* s, long* v) {
  char* e = 0; if (!*s) return false;
  *v = strtol(s, &e, 10);
  return *e == 0;
}

// Digits only (no sign), 0..4294967295.
static bool to_u32(const char* s, uint32_t* v) {
  if (!*s) return false;
  for (const char* p = s; *p; p++) if (*p < '0' || *p > '9') return false;
  unsigned long long x = strtoull(s, 0, 10);
  if (x > 0xFFFFFFFFull || strlen(s) > 10) return false;
  *v = (uint32_t)x;
  return true;
}

ppe_hook_t ppe_hook_parse(const char* line, ppe_hook_cmd_t* out) {
  ppe_hook_cmd_t d; d.cmd = PPE_HOOK_EMPTY; d.arg = 0; d.arg2 = 0; d.uarg = 0;
  char t[4][16]; int n = line ? tokens(line, t) : 0;
  if (n < 0) d.cmd = PPE_HOOK_UNKNOWN;
  else if (n == 0) d.cmd = PPE_HOOK_EMPTY;
  else if (!strcmp(t[0], "show") && n == 1) d.cmd = PPE_HOOK_SHOW;
  else if (!strcmp(t[0], "adc") && n == 2) {
    if (!strcmp(t[1], "fail")) d.cmd = PPE_HOOK_ADC_FAIL;
    else if (!strcmp(t[1], "ok")) d.cmd = PPE_HOOK_ADC_OK;
    else if (!strcmp(t[1], "real")) d.cmd = PPE_HOOK_ADC_REAL;
    else if (to_long(t[1], &d.arg) && d.arg >= 0 && d.arg <= PPE_ADC_MAX) d.cmd = PPE_HOOK_ADC_SET;
    else d.cmd = PPE_HOOK_BAD_ARG;
  }
  else if (!strcmp(t[0], "marker") && n == 2) {
    if (!strcmp(t[1], "on")) d.cmd = PPE_HOOK_MARKER_ON;
    else if (!strcmp(t[1], "off")) d.cmd = PPE_HOOK_MARKER_OFF;
    else d.cmd = PPE_HOOK_BAD_ARG;
  }
  else if (!strcmp(t[0], "set") && n == 3 && (!strcmp(t[1], "settle") || !strcmp(t[1], "grace"))) {
    bool settle = !strcmp(t[1], "settle");
    ppe_cfg_t probe; ppe_cfg_defaults(&probe);
    if (!to_long(t[2], &d.arg)) d.cmd = PPE_HOOK_BAD_ARG;
    else if (settle ? ppe_cfg_set_settle(&probe, d.arg) : ppe_cfg_set_grace(&probe, d.arg))   // same bounds as FR-40
      d.cmd = settle ? PPE_HOOK_SET_SETTLE : PPE_HOOK_SET_GRACE;
    else d.cmd = PPE_HOOK_BAD_ARG;
  }
  else if (!strcmp(t[0], "set") && n == 3 && !strcmp(t[1], "hb")) {
    ppe_cfg_t probe; ppe_cfg_defaults(&probe);
    d.cmd = (to_long(t[2], &d.arg) && ppe_cfg_set_heartbeat(&probe, d.arg)) ? PPE_HOOK_SET_HB : PPE_HOOK_BAD_ARG;   // same bounds as FR-40
  }
  else if (!strcmp(t[0], "tx") && n == 2) {
    if (!strcmp(t[1], "hb")) d.cmd = PPE_HOOK_TX_HB;
    else if (!strcmp(t[1], "worn")) { d.cmd = PPE_HOOK_TX_EVENT; d.arg = 1; }
    else if (!strcmp(t[1], "notworn")) { d.cmd = PPE_HOOK_TX_EVENT; d.arg = 0; }
    else d.cmd = PPE_HOOK_BAD_ARG;
  }
  else if (!strcmp(t[0], "setcounter") && n == 2) d.cmd = to_u32(t[1], &d.uarg) ? PPE_HOOK_SETCOUNTER : PPE_HOOK_BAD_ARG;
  else if (!strcmp(t[0], "skiptx") && n == 2) d.cmd = (to_long(t[1], &d.arg) && d.arg >= 0 && d.arg <= 100) ? PPE_HOOK_SKIPTX : PPE_HOOK_BAD_ARG;
  else if (!strcmp(t[0], "nvslog") && n == 2) {
    if (!strcmp(t[1], "on")) d.cmd = PPE_HOOK_NVSLOG_ON;
    else if (!strcmp(t[1], "off")) d.cmd = PPE_HOOK_NVSLOG_OFF;
    else d.cmd = PPE_HOOK_BAD_ARG;
  }
  else if (!strcmp(t[0], "fault") && n == 2) {
    if (!strcmp(t[1], "nvs-write")) d.cmd = PPE_HOOK_FAULT_NVS_WRITE;
    else if (!strcmp(t[1], "ble")) d.cmd = PPE_HOOK_FAULT_BLE;
    else if (!strcmp(t[1], "clear")) d.cmd = PPE_HOOK_FAULT_CLEAR;
    else d.cmd = PPE_HOOK_BAD_ARG;
  }
  else if (!strcmp(t[0], "awake") && n == 2) d.cmd = (to_long(t[1], &d.arg) && d.arg >= 1 && d.arg <= 3600) ? PPE_HOOK_AWAKE : PPE_HOOK_BAD_ARG;
  else if (!strcmp(t[0], "batt") && n == 2) {
    if (!strcmp(t[1], "fail")) d.cmd = PPE_HOOK_BATT_FAIL;
    else if (!strcmp(t[1], "real")) d.cmd = PPE_HOOK_BATT_REAL;
    else d.cmd = (to_long(t[1], &d.arg) && d.arg >= 0 && d.arg <= 5000) ? PPE_HOOK_BATT_SET : PPE_HOOK_BAD_ARG;   // millivolts
  }
  else if (!strcmp(t[0], "ident") && n == 4 && !strcmp(t[1], "set")) {
    d.cmd = (to_long(t[2], &d.arg) && d.arg >= 1 && d.arg <= 5 && to_long(t[3], &d.arg2) && d.arg2 >= 1 && d.arg2 <= 254) ? PPE_HOOK_IDENT_SET : PPE_HOOK_BAD_ARG;
  }
  else if (!strcmp(t[0], "ident") && n == 2) {
    if (!strcmp(t[1], "testkey")) d.cmd = PPE_HOOK_IDENT_TESTKEY;
    else if (!strcmp(t[1], "clear")) d.cmd = PPE_HOOK_IDENT_CLEAR;
    else if (!strcmp(t[1], "show")) d.cmd = PPE_HOOK_IDENT_SHOW;
    else d.cmd = PPE_HOOK_BAD_ARG;
  }
  else if (!strcmp(t[0], "sleep") && n == 2) {
    if (!strcmp(t[1], "on")) d.cmd = PPE_HOOK_SLEEP_ON;
    else if (!strcmp(t[1], "off")) d.cmd = PPE_HOOK_SLEEP_OFF;
    else d.cmd = PPE_HOOK_BAD_ARG;
  }
  else if (!strcmp(t[0], "adcat") && n == 3) {                 // the injected ADC reading becomes <value> after <seconds> (survives sleep)
    d.cmd = (to_long(t[1], &d.arg) && d.arg >= 1 && d.arg <= 86400 && to_long(t[2], &d.arg2) && d.arg2 >= 0 && d.arg2 <= 4095) ? PPE_HOOK_ADCAT : PPE_HOOK_BAD_ARG;
  }
  else if (!strcmp(t[0], "adcat") && n == 2) d.cmd = !strcmp(t[1], "clear") ? PPE_HOOK_ADCAT_CLEAR : PPE_HOOK_BAD_ARG;
  else if (!strcmp(t[0], "wakelog") && n == 1) d.cmd = PPE_HOOK_WAKELOG;
  else d.cmd = PPE_HOOK_UNKNOWN;
  if (out) *out = d;
  return d.cmd;
}

int ppe_mark_fmt(char* b, size_t n, const char* name, uint32_t us) {
  return snprintf(b, n, "MARK %s %lu", name, (unsigned long)us);
}
int ppe_mark_fmt_raw(char* b, size_t n, const char* name, uint32_t us, ppe_raw_t raw) {
  return snprintf(b, n, "MARK %s %lu state=%d fault=%d", name, (unsigned long)us, (int)raw.state, (int)raw.fault);
}
int ppe_cfg_fmt(char* b, size_t n, ppe_node_type_t type, const ppe_cfg_t* c) {
  return snprintf(b, n, "CFG type=%d settle_ms=%u grace_s=%u heartbeat_s=%u n=%d hooks=1",
                  (int)type, (unsigned)c->settle_ms, (unsigned)c->grace_s, (unsigned)c->heartbeat_s, PPE_FSR_SAMPLES);
}
#endif
