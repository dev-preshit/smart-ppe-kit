// ppe_log.h - logging macros. Compile-time level, runtime sink. NEVER pass key bytes to these.
//   PPE_LOGE/W/I/D(tag, fmt, ...)   level via -DPPE_LOG_LEVEL=0..4 (0 off, 1 E, 2 W, 3 I, 4 D; default 3)
// Arduino: output goes to Serial. Host: stderr. Override at runtime with ppe_log_set_sink().
#pragma once
#include <stdint.h>
#include <stdarg.h>

#ifndef PPE_LOG_LEVEL
#define PPE_LOG_LEVEL 3
#endif
enum { PPE_LVL_ERROR = 1, PPE_LVL_WARN = 2, PPE_LVL_INFO = 3, PPE_LVL_DEBUG = 4 };

typedef void (*ppe_log_sink_t)(const char* line);
void ppe_log_set_sink(ppe_log_sink_t sink);                       // nullptr restores the default
void ppe_log_write(int level, const char* tag, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

#define PPE_LOG_(lvl, tag, ...) do { if ((lvl) <= PPE_LOG_LEVEL) ppe_log_write((lvl), (tag), __VA_ARGS__); } while (0)
#define PPE_LOGE(tag, ...) PPE_LOG_(PPE_LVL_ERROR, tag, __VA_ARGS__)
#define PPE_LOGW(tag, ...) PPE_LOG_(PPE_LVL_WARN,  tag, __VA_ARGS__)
#define PPE_LOGI(tag, ...) PPE_LOG_(PPE_LVL_INFO,  tag, __VA_ARGS__)
#define PPE_LOGD(tag, ...) PPE_LOG_(PPE_LVL_DEBUG, tag, __VA_ARGS__)
