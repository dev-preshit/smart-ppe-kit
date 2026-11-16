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
#define HUB_SCAN_INTERVAL_MS    100u      // FR-20: continuous scan, window == interval (tuned in the Wi-Fi recheck)
#define HUB_SCAN_WINDOW_MS      100u

// S5 (hub logic): offline timing, tick, message queue
#define HUB_HEARTBEAT_S_DEFAULT 360u      // protocol spec 1.1
#define HUB_HEARTBEAT_S_MIN     60u
#define HUB_HEARTBEAT_S_MAX     3600u
#define HUB_MARGIN_S_DEFAULT    60u       // OFFLINE_MARGIN_S default, tuned at test (D-06)
#define HUB_MARGIN_S_MAX        300u
#define HUB_OFFLINE_MISSED      2u        // missed heartbeats before OFFLINE (spec 1.1)
#define HUB_TICK_MS             1000u     // spec 8.2: timeout evaluated at least once per second
#define HUB_MSG_QUEUE_DEPTH     16u       // pending hub messages between the pipeline and the sink
#define HUB_FW_ID               "s5-hub-logic"

// S5 part 2: motor timing (state machines 2.6, spec 8.3, FR-29, FR-30, NFR-05)
#define HUB_MOTOR_ON_US        300000u    // alert ON  300 +/- 30 ms
#define HUB_MOTOR_OFF_US       700000u    // alert OFF 700 +/- 30 ms
#define HUB_MOTOR_GUARD_US     400000u    // independent guard: output forced low 400 ms after the ON edge
#define HUB_KEY_ON_US          100000u    // KEY_FAULT pulse ON  100 +/- 20 ms
#define HUB_KEY_OFF_US         100000u    // KEY_FAULT pulse OFF 100 +/- 20 ms
#define HUB_KEY_REST_US       1700000u    // after the third pulse (V12-05: the low time is K_OFF + K_REST = 1800 ms)
#define HUB_KEY_PULSES         3u
#define HUB_MOTOR_RESYNC_US     20000u    // a pattern step later than this schedules from now, not from the nominal edge
#ifndef HUB_MOTOR_PIN
#define HUB_MOTOR_PIN          5          // motor driver input (owner decision 2026-11-15; override with -DHUB_MOTOR_PIN=n)
#endif
#define HUB_MOTOR_TASK_PRIO    12u        // above the rx task (10): the pattern must not wait for packet processing (X-R5)
#define HUB_MOTOR_GUARD_PERIOD_US 1000u   // the independent guard runs from its own esp_timer every 1 ms
