#include "hub_ble.h"
#include <NimBLEDevice.h>
#include "esp_timer.h"
#include "esp_system.h"
#include "hub_nvs.h"
#include "src/hub_scan_supervisor.h"
#ifdef PPE_TEST_HOOKS
#include "src/hub_dump.h"
#endif

// NimBLE host sync state. ble_hs_synced() is a C function inside the NimBLE host; NimBLEDevice::m_synced is private.
extern "C" int ble_hs_synced(void);

// HCI advertising report event type for ADV_NONCONN_IND (NimBLE hci_common.h: BLE_HCI_ADV_RPT_EVTYPE_NONCONN_IND).
// NimBLE-Arduino 2.5.1 without CONFIG_BT_NIMBLE_EXT_ADV (the default) hands the legacy event type to getAdvType().
static const uint8_t kEvtNonconnInd = 3;

struct RxEntry {
  uint64_t us;                  // esp_timer at callback entry (carried to the processing task, P-01)
  uint8_t  len;
  uint8_t  nonconn;
  uint8_t  data[HUB_RX_ADV_MAX];
};

static HubPipeline g_pipe;
static SemaphoreHandle_t g_pipe_mx = nullptr;
static QueueHandle_t g_rxq = nullptr;
static volatile uint32_t g_queue_full = 0, g_queue_max = 0;
static HubRxStats g_rx;
static HubScanSupervisor g_sup;
static volatile uint32_t g_scan_end_reason = 0, g_scan_end_count = 0;
#ifdef PPE_TEST_HOOKS
static volatile bool g_rxlog = false;          // default off (manager decision: keep it off during TC-72)
static volatile bool g_time = true;            // time stamp on every accepted packet
static volatile uint8_t g_forced_fail = 0;     // stall n: the next n restart attempts fail
#endif

RTC_NOINIT_ATTR static uint32_t g_rtc_magic;
RTC_NOINIT_ATTR static uint32_t g_rtc_fault;
static const uint32_t kRtcMagic = 0x50504530u;   // "PPE0"
static const uint32_t kFaultScanStall = 1;

// ---- BLE callback: copy and queue, nothing else (state machine P-01) -----------------------------------------
class RxScanCb : public NimBLEScanCallbacks {
 public:
  void onResult(const NimBLEAdvertisedDevice* d) override {
    RxEntry e;
    e.us = (uint64_t)esp_timer_get_time();
    const std::vector<uint8_t>& pl = d->getPayload();
    size_t n = pl.size();
    if (n > HUB_RX_ADV_MAX) n = HUB_RX_ADV_MAX;       // never 21, so the pipeline drops it as drop_len
    e.len = (uint8_t)n;
    e.nonconn = (d->getAdvType() == kEvtNonconnInd) ? 1 : 0;
    if (n) memcpy(e.data, pl.data(), n);
    if (xQueueSend(g_rxq, &e, 0) != pdTRUE) { g_queue_full = g_queue_full + 1; return; }
    UBaseType_t fill = uxQueueMessagesWaiting(g_rxq);
    if (fill > g_queue_max) g_queue_max = fill;
  }
  void onScanEnd(const NimBLEScanResults&, int reason) override {   // also fires when the host re-syncs
    g_scan_end_reason = (uint32_t)reason;
    g_scan_end_count = g_scan_end_count + 1;
  }
};
static RxScanCb g_cb;

// ---- processing -----------------------------------------------------------------------------------------------
static void log_result(const RxEntry& e, const HubResult& r, uint32_t now_ms) {
  (void)e;
#ifdef PPE_TEST_HOOKS
  if (g_time && r.verdict == HUB_V_ACCEPT)
    Serial.printf("ACC t_ms=%lu t_us=%llu node=%u ctr=%lu state=%u batt=%u flags=0x%02X missed=%lu\n", (unsigned long)now_ms,
                  (unsigned long long)esp_timer_get_time(), r.node_id, (unsigned long)r.counter, r.state, r.battery, r.flags,
                  (unsigned long)r.missed);
#endif
  if (r.verdict == HUB_V_REPLAY && r.anomaly_report)
    Serial.printf("ANOMALY t_ms=%lu node=%u received=%lu last=%lu\n", (unsigned long)now_ms, r.node_id,
                  (unsigned long)r.counter, (unsigned long)r.last_counter);
}

static void process_entry(const RxEntry& e) {
#ifdef PPE_TEST_HOOKS
  if (g_rxlog) {          // before filtering, duplicates included; timestamp comes from the queue entry (P-01)
    char hex[2 * HUB_RX_ADV_MAX + 1];
    for (uint8_t i = 0; i < e.len; i++) snprintf(hex + 2 * i, 3, "%02x", e.data[i]);
    hex[2 * e.len] = 0;
    Serial.printf("RX,%llu,%s\n", (unsigned long long)e.us, hex);
  }
#endif
  uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
  xSemaphoreTake(g_pipe_mx, portMAX_DELAY);
  HubResult r = g_pipe.process(e.data, e.len, e.nonconn != 0, now_ms);
  xSemaphoreGive(g_pipe_mx);
  if (r.verdict == HUB_V_ACCEPT) hub_persist_enqueue(r.node_id, r.counter);   // after dispatch, never inline
  uint32_t dt = (uint32_t)((uint64_t)esp_timer_get_time() - e.us);            // callback entry -> dispatched
  g_rx.count = g_rx.count + 1;
  g_rx.last_us = dt;
  if (dt > g_rx.max_us) g_rx.max_us = dt;
  if (dt > 10000u) g_rx.over_10ms = g_rx.over_10ms + 1;
  g_rx.sum_us = g_rx.sum_us + dt;
  log_result(e, r, now_ms);
}

static void rx_task(void*) {
  RxEntry e;
  for (;;) {
    if (xQueueReceive(g_rxq, &e, portMAX_DELAY) == pdTRUE) process_entry(e);
  }
}

// ---- scan supervisor (FR-21) --------------------------------------------------------------------------------
static bool scan_healthy() {
  return NimBLEDevice::getScan()->isScanning() && ble_hs_synced() != 0;
}

static bool scan_restart() {
#ifdef PPE_TEST_HOOKS
  if (g_forced_fail > 0) { g_forced_fail = g_forced_fail - 1; Serial.println("PPE_HOOK stall: restart attempt forced to fail"); return false; }
#endif
  NimBLEScan* s = NimBLEDevice::getScan();
  s->stop();
  bool ok = s->start(0, false, true);
  return ok && s->isScanning();
}

static void supervisor_task(void*) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(HUB_SCAN_POLL_MS));
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    bool healthy = scan_healthy();
    HubScanTick t = g_sup.tick(now_ms, healthy, hub_ble_adv_seen());
    if (t.quiet_hit) {
      xSemaphoreTake(g_pipe_mx, portMAX_DELAY);
      g_pipe.note_scan_quiet();
      xSemaphoreGive(g_pipe_mx);
      Serial.printf("SCAN quiet_60s t_ms=%lu (informational)\n", (unsigned long)now_ms);
    }
    if (t.action == HUB_SCAN_RESTART) {
      Serial.printf("SCAN restart #%lu t_ms=%lu synced=%d scanning=%d\n", (unsigned long)g_sup.restarts(),
                    (unsigned long)now_ms, ble_hs_synced() != 0, NimBLEDevice::getScan()->isScanning());
      bool ok = scan_restart();
      g_sup.restart_done((uint32_t)(esp_timer_get_time() / 1000), ok);
      Serial.printf("SCAN restart result=%s fails=%lu\n", ok ? "ok" : "failed", (unsigned long)g_sup.consecutive_fails());
    } else if (t.action == HUB_SCAN_REBOOT) {
      Serial.printf("SCAN reboot t_ms=%lu after %lu failed restarts\n", (unsigned long)now_ms,
                    (unsigned long)g_sup.consecutive_fails());
      Serial.flush();
      g_rtc_magic = kRtcMagic;
      g_rtc_fault = kFaultScanStall;   // logged after the reboot; the HUB_FAULT uplink is S7
      delay(50);
      ESP.restart();
    }
  }
}

// ---- start ------------------------------------------------------------------------------------------------------
bool hub_ble_start(uint8_t vest_id, const uint8_t* key32, HubStore* store) {
  if (g_rtc_magic == kRtcMagic) {
    Serial.printf("BOOT previous run ended with fault code %lu (1 = scan stall reboot)\n", (unsigned long)g_rtc_fault);
    g_rtc_magic = 0;
  }
  Serial.printf("BOOT reset_reason=%d\n", (int)esp_reset_reason());
  g_pipe_mx = xSemaphoreCreateMutex();
  g_rxq = xQueueCreate(HUB_RX_QUEUE_DEPTH, sizeof(RxEntry));
  if (!g_pipe_mx || !g_rxq) { Serial.println("ERROR queue or mutex allocation failed"); return false; }

  if (!g_pipe.init(vest_id, key32)) { Serial.println("ERROR pipeline init refused"); return false; }
  bool reg = g_pipe.load_registry(store, (uint32_t)(esp_timer_get_time() / 1000));
  Serial.printf("BOOT registry_ok=%d accept_enabled=%d vest=0x%02X\n", reg, g_pipe.accept_enabled(), vest_id);
  for (uint8_t i = 1; i <= HUB_NODE_SLOTS; i++)
    Serial.printf("BOOT node %u last_counter=%lu state=%s\n", i, (unsigned long)g_pipe.node(i).last_counter,
                  hub_node_state_name(g_pipe.node(i).state));

  xTaskCreate(rx_task, "ppe_rx", 6144, nullptr, 10, nullptr);

  NimBLEDevice::init("");
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&g_cb, true);        // wantDuplicates: duplicate filtering OFF (S-01, TC-24 needs the copies)
  scan->setActiveScan(false);                 // passive: ADV_NONCONN_IND needs no scan response
  scan->setInterval(HUB_SCAN_INTERVAL_MS);
  scan->setWindow(HUB_SCAN_WINDOW_MS);
  scan->setMaxResults(0);                     // keep no result list, callbacks only
  bool ok = scan->start(0, false, true);      // duration 0 = scan until stopped
  g_sup.start((uint32_t)(esp_timer_get_time() / 1000), 0);
  Serial.printf("BOOT scan started=%d interval=%u ms window=%u ms\n", ok, (unsigned)HUB_SCAN_INTERVAL_MS, (unsigned)HUB_SCAN_WINDOW_MS);
  xTaskCreate(supervisor_task, "ppe_sup", 4096, nullptr, 3, nullptr);
  return ok;
}

HubPipeline& hub_pipeline() { return g_pipe; }
void hub_pipeline_lock() { xSemaphoreTake(g_pipe_mx, portMAX_DELAY); }
void hub_pipeline_unlock() { xSemaphoreGive(g_pipe_mx); }
const HubRxStats& hub_rx_stats() { return g_rx; }
uint32_t hub_ble_queue_full() { return g_queue_full; }
uint32_t hub_ble_queue_fill() { return g_rxq ? (uint32_t)uxQueueMessagesWaiting(g_rxq) : 0; }
uint32_t hub_ble_queue_max_fill() { return g_queue_max; }
uint32_t hub_ble_adv_seen() { return g_pipe.global().rx_total + g_queue_full; }
uint32_t hub_ble_scan_restarts() { return g_sup.restarts(); }
uint32_t hub_ble_scan_consecutive_fails() { return g_sup.consecutive_fails(); }

#ifdef PPE_TEST_HOOKS
bool hub_hook_inject(const uint8_t* adv, size_t len) {
  if (!g_rxq || len > HUB_RX_ADV_MAX) return false;
  RxEntry e;
  e.us = (uint64_t)esp_timer_get_time();
  e.len = (uint8_t)len;
  e.nonconn = 1;                                   // injected packets look like ADV_NONCONN_IND
  if (len) memcpy(e.data, adv, len);
  if (xQueueSend(g_rxq, &e, 0) != pdTRUE) { g_queue_full = g_queue_full + 1; return false; }
  UBaseType_t fill = uxQueueMessagesWaiting(g_rxq);
  if (fill > g_queue_max) g_queue_max = fill;
  return true;
}

void hub_hook_stall(uint8_t n) {
  g_forced_fail = n;
  NimBLEDevice::getScan()->stop();                 // a real stall: no advertisements until a restart works
  Serial.printf("PPE_HOOK stall %u t_ms=%lu scan stopped\n", n, (unsigned long)(esp_timer_get_time() / 1000));
}

static void dump_emit(void*, const char* line) { Serial.println(line); }

void hub_hook_dump() {
  HubDumpExtra x;
  memset(&x, 0, sizeof x);
  const HubPersistStats& ps = hub_persist_stats();
  x.now_ms = (uint32_t)(esp_timer_get_time() / 1000);
  x.queue_fill = hub_ble_queue_fill(); x.queue_depth = HUB_RX_QUEUE_DEPTH;
  x.queue_max_fill = g_queue_max; x.queue_full = g_queue_full;
  x.persist_pending = hub_persist_pending(); x.persist_writes = ps.writes; x.persist_write_fails = hub_persist_writes_failed();
  x.persist_dropped = ps.queue_dropped; x.persist_last_us = ps.last_us; x.persist_max_us = ps.max_us;
  x.persist_avg_us = ps.writes ? (uint32_t)(ps.sum_us / ps.writes) : 0;
  x.proc_count = g_rx.count; x.proc_last_us = g_rx.last_us; x.proc_max_us = g_rx.max_us; x.proc_over_10ms = g_rx.over_10ms;
  x.proc_avg_us = g_rx.count ? (uint32_t)(g_rx.sum_us / g_rx.count) : 0;
  x.scan_restarts = g_sup.restarts(); x.scan_consecutive_fails = g_sup.consecutive_fails();
  // Copy the pipeline under the lock, print after releasing it. Serial output takes milliseconds, and holding the
  // lock during it made the receive task wait and inflated the processing time (bench finding, 2026-10-20).
  HubPipeline snap;
  xSemaphoreTake(g_pipe_mx, portMAX_DELAY);
  snap = g_pipe;
  xSemaphoreGive(g_pipe_mx);
  hub_dump_emit(snap, x, dump_emit, nullptr);
}

void hub_hook_time_now() {
  Serial.printf("PPE_HOOK time t_ms=%lu t_us=%llu\n", (unsigned long)(esp_timer_get_time() / 1000), (unsigned long long)esp_timer_get_time());
}
void hub_hook_set_time(bool on) { g_time = on; Serial.printf("PPE_HOOK time %s\n", on ? "on" : "off"); }
void hub_hook_set_rxlog(bool on) { g_rxlog = on; Serial.printf("PPE_HOOK rxlog %s\n", on ? "on" : "off"); }
#endif
