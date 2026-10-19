// Hub constants shared by the pure logic and the Arduino glue. No Arduino includes here.
#pragma once
#include <stdint.h>

#define HUB_NODE_SLOTS          5u        // node ids 1..5 (spec 8.1, FR-22)
#define HUB_ANOMALY_LIMIT_MS    60000u    // COUNTER_ANOMALY: once per node per 60 s (spec 6.2)
#define HUB_SCAN_STALL_MS       10000u    // FR-21: scanner not healthy for 10 s -> restart
#define HUB_SCAN_MAX_FAILS      3u        // FR-21: 3 consecutive failed restarts -> reboot
#define HUB_SCAN_POLL_MS        1000u     // FR-21: scanner state polled every 1 s
#define HUB_QUIET_WINDOW_MS     60000u    // spec 9.3: scan_quiet_60s window (log only)
#define HUB_RX_QUEUE_DEPTH      64u       // callback -> task queue (state machine P-01: >= 32)
#define HUB_RX_ADV_MAX          64u       // bytes copied per advertisement (legacy max is 31)
#define HUB_PERSIST_QUEUE_DEPTH 32u       // task -> NVS writer queue
