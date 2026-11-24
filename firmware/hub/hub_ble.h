// BLE receive glue (Arduino + NimBLE-Arduino 2.5.1 + FreeRTOS). Pure logic lives in src/.
// Data path:  NimBLE host task -> onResult (copy to queue, nothing else) -> rx task -> HubPipeline.process
//             -> HubPersist queue (NVS writer task).   Supervisor task polls the scanner every 1 s (FR-21).
#pragma once
#include <Arduino.h>
#include "src/hub_pipeline.h"
#include "src/hub_msg.h"

struct HubRxStats {              // callback entry -> processing finished, per advertisement (TC-72)
  volatile uint32_t count;       // advertisements processed by the rx task
  volatile uint32_t last_us, max_us;
  volatile uint32_t over_10ms;   // NFR-03 limit is 10 ms
  volatile uint64_t sum_us;
};

// Initialises the pipeline from the provisioning data, loads the registry from `store`, starts the rx task,
// the supervisor task and the continuous scan. Returns false if the scan could not be started.
// key32 == nullptr is the B-04 path: no key, accept disabled, KEY_FAULT pattern, HUB_FAULT (NO_KEY). store == nullptr or an
// unreadable store is the B-04b path: accept disabled, nodes go OFFLINE, HUB_FAULT (REGISTRY). params_ok=false reports NVS_READ.
bool hub_ble_start(uint8_t vest_id, const uint8_t* key32, HubStore* store, bool params_ok = true);
void hub_apply_params(uint32_t heartbeat_s, uint32_t margin_s);   // provisioned timing read from NVS at boot (bounds are checked)

// Where hub messages (COMPLIANCE, NODE_OFFLINE, HUB_BOOT, HUB_FAULT) go. Default: one serial line each (S5).
// S7 plugs its uplink queue in here; nothing else in S5 changes.
void hub_set_sink(HubSink* sink);

HubPipeline& hub_pipeline();                 // the one pipeline instance (dump reads it under the lock)
void hub_pipeline_lock();
void hub_pipeline_unlock();
const HubRxStats& hub_rx_stats();
uint32_t hub_ble_queue_full();               // callbacks dropped because the rx queue was full (internal)
uint32_t hub_ble_queue_fill();
uint32_t hub_ble_queue_max_fill();
uint32_t hub_ble_adv_seen();                 // callbacks seen = rx_total + queue_full
uint32_t hub_ble_scan_restarts();
uint32_t hub_ble_scan_consecutive_fails();

#ifdef PPE_TEST_HOOKS
// Test hooks (serial commands in hub.ino call these). Compiled only with -DPPE_TEST_HOOKS.
bool hub_hook_inject(const uint8_t* adv, size_t len);   // same queue and rx task as a BLE advertisement
void hub_hook_stall(uint8_t n);                         // stop the scan; the next n restart attempts fail
void hub_hook_dump();                                   // registry, counters, queue fill, timing
void hub_hook_time_now();
void hub_hook_set_time(bool on);                        // time stamp on every accepted packet (default on)
void hub_hook_set_rxlog(bool on);                       // every received advertisement before filtering (default off)
void hub_hook_set_margin(uint32_t s);                   // OFFLINE_MARGIN_S 0..300 (hub provisioning is S6)
void hub_hook_set_heartbeat(uint32_t s);                // HEARTBEAT_S 60..3600
// fault <wdt|nvs-read|nvs-write|registry|key|guard> (TC-30). The restart cases keep a one-shot flag in RTC memory.
void hub_hook_fault(uint8_t which);
#define HUB_FORCE_NO_KEY    1u
#define HUB_FORCE_REGISTRY  2u
#define HUB_FORCE_PARAMS    4u
uint32_t hub_take_boot_forces();                        // flags set before the last restart; cleared by the call
#endif
