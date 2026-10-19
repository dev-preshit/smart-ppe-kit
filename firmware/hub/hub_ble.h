// BLE receive glue (Arduino + NimBLE-Arduino 2.5.1 + FreeRTOS). Pure logic lives in src/.
// Data path:  NimBLE host task -> onResult (copy to queue, nothing else) -> rx task -> HubPipeline.process
//             -> HubPersist queue (NVS writer task).   Supervisor task polls the scanner every 1 s (FR-21).
#pragma once
#include <Arduino.h>
#include "src/hub_pipeline.h"

struct HubRxStats {              // callback entry -> processing finished, per advertisement (TC-72)
  volatile uint32_t count;       // advertisements processed by the rx task
  volatile uint32_t last_us, max_us;
  volatile uint32_t over_10ms;   // NFR-03 limit is 10 ms
  volatile uint64_t sum_us;
};

// Initialises the pipeline from the provisioning data, loads the registry from `store`, starts the rx task,
// the supervisor task and the continuous scan. Returns false if the scan could not be started.
bool hub_ble_start(uint8_t vest_id, const uint8_t* key32, HubStore* store);

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
