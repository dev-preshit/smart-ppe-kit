#include "hub_nvs.h"
#include "esp_timer.h"

static const char* const kNs = "ppehub";
static const char* const kKeys[HUB_NODE_SLOTS + 1] = {"", "lc1", "lc2", "lc3", "lc4", "lc5"};

bool HubNvsStore::begin() {
  open_ = prefs_.begin(kNs, false);   // read/write so the namespace exists on first boot
  return open_;
}

HubStoreResult HubNvsStore::read_last_counter(uint8_t node_id, uint32_t* value) {
  if (!open_ || node_id < 1 || node_id > HUB_NODE_SLOTS) return HUB_STORE_ERROR;
  if (!prefs_.isKey(kKeys[node_id])) return HUB_STORE_NOT_FOUND;
  *value = prefs_.getUInt(kKeys[node_id], 0);
  return HUB_STORE_OK;
}

bool HubNvsStore::write_last_counter(uint8_t node_id, uint32_t value) {
  if (!open_ || node_id < 1 || node_id > HUB_NODE_SLOTS) return false;
  return prefs_.putUInt(kKeys[node_id], value) == sizeof(uint32_t);   // putUInt commits
}

struct PersistItem { uint8_t node_id; uint32_t counter; };
static QueueHandle_t g_pq = nullptr;
static HubPersist g_persist;
static HubNvsStore* g_store = nullptr;
static HubPersistStats g_stats;

static void persist_task(void*) {
  PersistItem it;
  for (;;) {
    if (xQueueReceive(g_pq, &it, portMAX_DELAY) != pdTRUE) continue;
    g_persist.note(it.node_id, it.counter);
    while (xQueueReceive(g_pq, &it, 0) == pdTRUE) g_persist.note(it.node_id, it.counter);   // coalesce
    uint8_t n; uint32_t c;
    while (g_persist.next(&n, &c)) {
      uint64_t t0 = esp_timer_get_time();
      bool ok = g_store->write_last_counter(n, c);
      uint32_t dt = (uint32_t)(esp_timer_get_time() - t0);
      g_stats.writes++;
      g_stats.last_us = dt;
      if (dt > g_stats.max_us) g_stats.max_us = dt;
      g_stats.sum_us += dt;
      g_persist.done(n, c, ok);
    }
  }
}

bool hub_persist_start(HubNvsStore* store) {
  g_store = store;
  g_pq = xQueueCreate(HUB_PERSIST_QUEUE_DEPTH, sizeof(PersistItem));
  if (!g_pq) return false;
  // Priority 1 = lowest above idle: never delays the BLE host or the rx task (brief story 2).
  return xTaskCreate(persist_task, "ppe_persist", 4096, nullptr, 1, nullptr) == pdPASS;
}

void hub_persist_enqueue(uint8_t node_id, uint32_t counter) {
  PersistItem it = {node_id, counter};
  if (!g_pq || xQueueSend(g_pq, &it, 0) != pdTRUE) g_stats.queue_dropped++;
}

const HubPersistStats& hub_persist_stats() { return g_stats; }
unsigned hub_persist_pending() { return g_persist.pending() + (g_pq ? (unsigned)uxQueueMessagesWaiting(g_pq) : 0u); }
uint32_t hub_persist_writes_failed() { return g_persist.writes_failed(); }
