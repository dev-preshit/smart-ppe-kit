// NVS glue for last_counter (Arduino only). The pure logic is in src/hub_persist.*, src/hub_pipeline.*.
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "src/hub_pipeline.h"
#include "src/hub_persist.h"

// Preferences namespace "ppehub", keys lc1..lc5 (one uint32 per node, spec 6.2).
class HubNvsStore : public HubStore {
 public:
  bool begin();                                   // false = NVS not usable: registry stays disabled
  HubStoreResult read_last_counter(uint8_t node_id, uint32_t* value) override;
  bool write_last_counter(uint8_t node_id, uint32_t value) override;
 private:
  Preferences prefs_;
  bool open_ = false;
};

struct HubPersistStats {          // measured around every NVS write (brief story 2, TC-72)
  volatile uint32_t writes;       // writes attempted
  volatile uint32_t last_us, max_us;
  volatile uint64_t sum_us;
  volatile uint32_t queue_dropped;   // enqueue failed because the writer queue was full
};

bool hub_persist_start(HubNvsStore* store);                    // queue + low-priority writer task
void hub_persist_enqueue(uint8_t node_id, uint32_t counter);   // non-blocking, safe from the rx task
const HubPersistStats& hub_persist_stats();
unsigned hub_persist_pending();
uint32_t hub_persist_writes_failed();
