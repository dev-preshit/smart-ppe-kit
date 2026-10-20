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
  }
  return c;
}
#endif
