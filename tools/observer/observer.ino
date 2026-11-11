// observer.ino v3 - S4 bench tool: passive BLE scan that logs every advertising packet (A-14). TEST TOOL, never part of a release.
// Board: ESP32C3 Dev Module, USB CDC On Boot: Enabled. No ppe_common, no key, no build_opt.h. Serial 115200.
// Passive scan, window == interval (continuous, like the hub), duplicates NOT filtered.
// One line per packet:  PKT <sec>.<micro> <len> <evt> <rssi> <hex>
//   time = esp_timer_get_time() at callback entry (NimBLE host task, not the radio edge: tools/observe.py calibrate measures the error)
//   evt  = HCI advertising event type (3 = ADV_NONCONN_IND, 0 = ADV_IND, 2 = ADV_SCAN_IND)
// Also: "LOST <n>" when the queue overflowed (total so far).
// Commands, one per line:  PING -> PONG | FILTER <n> keep only payloads of n bytes, applied BEFORE the queue (0 = all, default 0) | STAT | TIME
//   RESET -> empties the queue, zeroes the counters, replies 'OK reset' (every capture starts with it, so STAT counts only that capture)
//   Once a second: SEC <s> seen= skipped= printed= lost= q=<queue depth>  (shows when and why packets are lost)
// v2: v1 lost 90% of the packets in a busy room (queue full, one printf per byte, LOST printed on every loop pass). Now: filter in the
//     callback, one write per line, LOST at most once a second, queue 256. 'lost' counts only packets that passed the filter.
#include <NimBLEDevice.h>
#include "esp_timer.h"

#define OBS_INTERVAL_MS 100
#define OBS_WINDOW_MS   100
#define QUEUE_LEN       256

struct Pkt { uint64_t us; uint8_t len; uint8_t evt; int8_t rssi; uint8_t data[31]; };

static QueueHandle_t q = nullptr;
static volatile uint32_t seen = 0, skipped = 0, lost = 0;
static uint32_t printed = 0, lastLost = 0, lastLostMs = 0, lastSecMs = 0;
static volatile uint8_t filterLen = 0;
static char line[64];
static size_t lineLen = 0;

class ObsCb : public NimBLEScanCallbacks {
 public:
  void onResult(const NimBLEAdvertisedDevice* d) override {
    Pkt p;
    p.us = (uint64_t)esp_timer_get_time();
    const std::vector<uint8_t>& pl = d->getPayload();
    size_t n = pl.size();
    seen = seen + 1;
    if (filterLen && n != filterLen) { skipped = skipped + 1; return; }
    if (n > sizeof p.data) n = sizeof p.data;
    p.len = (uint8_t)n;
    p.evt = (uint8_t)d->getAdvType();
    p.rssi = (int8_t)d->getRSSI();
    if (n) memcpy(p.data, pl.data(), n);
    if (xQueueSend(q, &p, 0) != pdTRUE) lost = lost + 1;
  }
};
static ObsCb cb;

static void printPkt(const Pkt& p) {
  char buf[128];
  int n = snprintf(buf, sizeof buf, "PKT %lu.%06lu %u %u %d ", (unsigned long)(p.us / 1000000ULL), (unsigned long)(p.us % 1000000ULL),
                   (unsigned)p.len, (unsigned)p.evt, (int)p.rssi);
  for (uint8_t i = 0; i < p.len && n < (int)sizeof buf - 4; i++) n += snprintf(buf + n, sizeof buf - n, "%02x", p.data[i]);
  buf[n++] = '\n';
  Serial.write((const uint8_t*)buf, (size_t)n);
  printed++;
}

static void handleLine(const char* s) {
  if (!strcmp(s, "PING")) Serial.println("PONG");
  else if (!strncmp(s, "FILTER ", 7)) { filterLen = (uint8_t)atoi(s + 7); Serial.printf("OK filter=%u\n", (unsigned)filterLen); }
  else if (!strcmp(s, "STAT")) Serial.printf("STAT seen=%lu skipped=%lu printed=%lu lost=%lu filter=%u\n", (unsigned long)seen, (unsigned long)skipped, (unsigned long)printed, (unsigned long)lost, (unsigned)filterLen);
  else if (!strcmp(s, "RESET")) { xQueueReset(q); seen = 0; skipped = 0; lost = 0; printed = 0; lastLost = 0; Serial.println("OK reset"); }
  else if (!strcmp(s, "TIME")) { uint64_t us = (uint64_t)esp_timer_get_time(); Serial.printf("TIME %lu.%06lu\n", (unsigned long)(us / 1000000ULL), (unsigned long)(us % 1000000ULL)); }
  else if (s[0]) Serial.println("ERR unknown command (PING | FILTER <n> | STAT | TIME | RESET)");
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);
  delay(200);
  q = xQueueCreate(QUEUE_LEN, sizeof(Pkt));
  NimBLEDevice::init("");
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&cb, true);      // wantDuplicates: duplicate filtering OFF
  scan->setActiveScan(false);             // passive
  scan->setInterval(OBS_INTERVAL_MS);
  scan->setWindow(OBS_WINDOW_MS);
  scan->setMaxResults(0);                 // callbacks only, no result list
  bool ok = scan->start(0, false, true);  // duration 0 = until stopped
  Serial.printf("OBSERVER v3 ready scan=%d interval=%u ms window=%u ms\n", ok, (unsigned)OBS_INTERVAL_MS, (unsigned)OBS_WINDOW_MS);
}

void loop() {
  Pkt p;
  while (xQueueReceive(q, &p, 0) == pdTRUE) printPkt(p);
  if (lost != lastLost && millis() - lastLostMs >= 1000) { lastLost = lost; lastLostMs = millis(); Serial.printf("LOST %lu\n", (unsigned long)lost); }
  if (millis() - lastSecMs >= 1000) {
    lastSecMs = millis();
    Serial.printf("SEC %lu seen=%lu skipped=%lu printed=%lu lost=%lu q=%u\n", (unsigned long)(esp_timer_get_time() / 1000000ULL), (unsigned long)seen,
                  (unsigned long)skipped, (unsigned long)printed, (unsigned long)lost, (unsigned)uxQueueMessagesWaiting(q));
  }
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') { line[lineLen] = 0; lineLen = 0; handleLine(line); }
    else if (lineLen < sizeof line - 1) line[lineLen++] = c;
    else lineLen = 0;
  }
  delay(1);
}
