#include "hub_hooks.h"
#ifdef PPE_TEST_HOOKS
#include <string.h>

static bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

static int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Hex digits in pairs; spaces, ':' and ',' between bytes are ignored. False on a bad digit, odd count or overflow.
static bool parse_hex(const char* s, uint8_t* out, size_t max, uint8_t* len) {
  size_t n = 0; int hi = -1;
  for (; *s; s++) {
    char c = *s;
    if (is_ws(c) || c == ':' || c == ',') { if (hi >= 0) return false; continue; }   // separator inside a byte
    int v = hexval(c);
    if (v < 0) return false;
    if (hi < 0) { hi = v; }
    else { if (n >= max) return false; out[n++] = (uint8_t)((hi << 4) | v); hi = -1; }
  }
  if (hi >= 0) return false;
  *len = (uint8_t)n;
  return true;
}

// 1 to 6 decimal digits and nothing else.
static bool parse_u32(const char* s, size_t len, uint32_t* out) {
  if (len == 0 || len > 6) return false;
  uint32_t v = 0;
  for (size_t i = 0; i < len; i++) {
    if (s[i] < '0' || s[i] > '9') return false;
    v = v * 10u + (uint32_t)(s[i] - '0');
  }
  *out = v;
  return true;
}

// Exactly `want` numbers (1 to 6 digits each) separated by blanks, nothing else.
static bool parse_nums(const char* s, uint32_t* out, int want) {
  int n = 0;
  for (;;) {
    while (*s && is_ws(*s)) s++;
    if (!*s) break;
    const char* b = s;
    while (*s && !is_ws(*s)) s++;
    if (n >= want || !parse_u32(b, (size_t)(s - b), &out[n])) return false;
    n++;
  }
  return n == want;
}

HubHookCmd hub_hook_parse(const char* line) {
  HubHookCmd c; memset(&c, 0, sizeof c);
  while (*line && is_ws(*line)) line++;
  if (!*line) { c.type = HUB_HOOK_NONE; return c; }
  c.type = HUB_HOOK_ERROR;
  const char* e = line; while (*e && !is_ws(*e)) e++;
  size_t wl = (size_t)(e - line);
  const char* arg = e; while (*arg && is_ws(*arg)) arg++;
  size_t al = strlen(arg); while (al && is_ws(arg[al - 1])) al--;     // trim the right side

  if (wl == 6 && !strncmp(line, "inject", 6)) {
    if (parse_hex(arg, c.data, sizeof c.data, &c.len)) c.type = HUB_HOOK_INJECT;
  } else if (wl == 5 && !strncmp(line, "stall", 5)) {
    if (al == 1 && arg[0] >= '1' && arg[0] <= '9') { c.n = (uint8_t)(arg[0] - '0'); c.type = HUB_HOOK_STALL; }
  } else if (wl == 4 && !strncmp(line, "dump", 4)) {
    if (al == 0) c.type = HUB_HOOK_DUMP;
  } else if (wl == 4 && !strncmp(line, "time", 4)) {
    if (al == 0) c.type = HUB_HOOK_TIME_NOW;
    else if (al == 2 && !strncmp(arg, "on", 2)) c.type = HUB_HOOK_TIME_ON;
    else if (al == 3 && !strncmp(arg, "off", 3)) c.type = HUB_HOOK_TIME_OFF;
  } else if (wl == 5 && !strncmp(line, "rxlog", 5)) {
    if (al == 2 && !strncmp(arg, "on", 2)) c.type = HUB_HOOK_RXLOG_ON;
    else if (al == 3 && !strncmp(arg, "off", 3)) c.type = HUB_HOOK_RXLOG_OFF;
  } else if (wl == 5 && !strncmp(line, "motor", 5)) {
    if (al == 0) c.type = HUB_HOOK_MOTOR;
  } else if (wl == 6 && !strncmp(line, "marker", 6)) {
    if (al == 2 && !strncmp(arg, "on", 2)) c.type = HUB_HOOK_MARKER_ON;
    else if (al == 3 && !strncmp(arg, "off", 3)) c.type = HUB_HOOK_MARKER_OFF;
  } else if (wl == 5 && !strncmp(line, "pulse", 5)) {
    uint32_t v[3];
    if (parse_nums(arg, v, 3)) { c.val = v[0]; c.val2 = v[1]; c.val3 = v[2]; c.type = HUB_HOOK_PULSE; }
  } else if (wl == 3 && !strncmp(line, "set", 3)) {
    const char* k = arg; while (*k && !is_ws(*k)) k++;                       // end of the key word
    size_t kl = (size_t)(k - arg);
    const char* v = k; while (*v && is_ws(*v)) v++;
    size_t vl = strlen(v); while (vl && is_ws(v[vl - 1])) vl--;
    uint32_t val = 0;
    if (parse_u32(v, vl, &val)) {
      if (kl == 6 && !strncmp(arg, "margin", 6)) { c.val = val; c.type = HUB_HOOK_SET_MARGIN; }
      else if (kl == 9 && !strncmp(arg, "heartbeat", 9)) { c.val = val; c.type = HUB_HOOK_SET_HEARTBEAT; }
    }
  }
  return c;
}
#endif
