#include "ppe_log.h"
#include <stdio.h>
#ifdef ARDUINO
#include <Arduino.h>
#endif

static ppe_log_sink_t g_sink = nullptr;
void ppe_log_set_sink(ppe_log_sink_t s) { g_sink = s; }

static void default_sink(const char* line) {
#ifdef ARDUINO
  Serial.println(line);
#else
  fprintf(stderr, "%s\n", line);
#endif
}

void ppe_log_write(int level, const char* tag, const char* fmt, ...) {
  static const char L[] = {'?', 'E', 'W', 'I', 'D'};
  char msg[160], line[200];
  va_list ap; va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
#ifdef ARDUINO
  unsigned long ms = (unsigned long)millis();
#else
  unsigned long ms = 0;
#endif
  snprintf(line, sizeof line, "[%lu] %c %s: %s", ms, (level >= 1 && level <= 4) ? L[level] : L[0], tag, msg);
  (g_sink ? g_sink : default_sink)(line);
}
