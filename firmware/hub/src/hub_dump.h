// "dump" hook output (registry, last_counter, last_seen, counters, queue fill). Test builds only.
#pragma once
#ifdef PPE_TEST_HOOKS
#include <stdint.h>
#include "hub_pipeline.h"

struct HubDumpExtra {               // values owned by the glue (queue, tasks, timing)
  uint32_t now_ms;
  uint32_t queue_fill, queue_depth, queue_max_fill, queue_full;
  uint32_t persist_pending, persist_writes, persist_write_fails, persist_dropped;
  uint32_t persist_last_us, persist_max_us, persist_avg_us;
  uint32_t proc_count, proc_last_us, proc_max_us, proc_avg_us, proc_over_10ms;
  uint32_t scan_restarts, scan_consecutive_fails;
};

typedef void (*HubDumpEmit)(void* ctx, const char* line);
// Every line starts with "DUMP " and uses key=value pairs, so logs can be parsed by tools/s3_capture.py.
void hub_dump_emit(const HubPipeline& h, const HubDumpExtra& x, HubDumpEmit emit, void* ctx);
#endif
