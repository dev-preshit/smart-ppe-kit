// S0 BLE spike - HUB test sketch (ESP32-S3)
// Board: "ESP32S3 Dev Module", USB CDC On Boot: Enabled
// Continuous passive scan; verifies HMAC, tracks counters per node, prints a summary every 10 s.
#include <NimBLEDevice.h>
#include "ppe_proto.h"

struct NodeStat {
  bool     seen = false;
  uint32_t lastCtr = 0;
  uint32_t newPkts = 0;     // distinct counters accepted
  uint32_t dups = 0;        // repeats of same counter within a burst
  uint32_t gaps = 0;        // counters skipped (= missed wake bursts)
  uint32_t replays = 0;     // counter went backwards
  uint32_t bad = 0;         // bad-MAC counters (distinct)
  uint32_t lastBadCtr = 0xFFFFFFFF;
  int      rssi = 0;
  uint32_t lastMs = 0;
};
NodeStat stats[6];          // index = nodeId 1..5

// diagnostics: is scanning working at all?
volatile uint32_t dTotal = 0, dMfg = 0, dBadLen = 0, dBadCompany = 0, dOurs = 0;
volatile uint32_t dShown = 0;

class ScanCB : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* d) override {
    dTotal++;
    if (!d->haveManufacturerData()) return;
    dMfg++;
    std::string md = d->getManufacturerData();
    if (md.size() != sizeof(PpePacket)) {
      dBadLen++;
      return;
    }

    PpePacket pkt;
    memcpy(&pkt, md.data(), sizeof(pkt));
    if (pkt.company != PPE_COMPANY_ID || pkt.ver != PPE_PROTO_VER) {
      dBadCompany++;
      return;
    }
    if (pkt.nodeId < 1 || pkt.nodeId > 5) return;
    dOurs++;

    NodeStat& s = stats[pkt.nodeId];
    int rssi = d->getRSSI();

    if (!ppeVerifyMac(pkt)) {
      if (s.lastBadCtr != pkt.counter) {
        s.lastBadCtr = pkt.counter;
        s.bad++;
        Serial.printf("[BAD MAC ] %s ctr=%lu rssi=%d  -> rejected\n",
                      PPE_NODE_NAMES[pkt.nodeId], (unsigned long)pkt.counter, rssi);
      }
      return;
    }

    s.rssi = rssi;
    s.lastMs = millis();

    if (!s.seen) {
      s.seen = true; s.lastCtr = pkt.counter; s.newPkts++;
      Serial.printf("[FIRST   ] %s ctr=%lu rssi=%d\n",
                    PPE_NODE_NAMES[pkt.nodeId], (unsigned long)pkt.counter, rssi);
      return;
    }
    if (pkt.counter == s.lastCtr) { s.dups++; return; }
    if (pkt.counter <  s.lastCtr) {
      s.replays++;
      Serial.printf("[REPLAY  ] %s ctr=%lu (last=%lu) -> rejected\n",
                    PPE_NODE_NAMES[pkt.nodeId], (unsigned long)pkt.counter, (unsigned long)s.lastCtr);
      return;
    }
    uint32_t gap = pkt.counter - s.lastCtr - 1;
    s.gaps += gap;
    s.lastCtr = pkt.counter;
    s.newPkts++;
    Serial.printf("[OK      ] %s ctr=%lu rssi=%d%s\n",
                  PPE_NODE_NAMES[pkt.nodeId], (unsigned long)pkt.counter, rssi,
                  gap ? "  <-- gap before this" : "");
  }
};
ScanCB scanCb;
uint32_t lastSummary = 0;

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 5000) delay(10);   // wait for monitor
  delay(300);
  Serial.println("[hub] S0 BLE spike hub starting");

  NimBLEDevice::init("hub");
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&scanCb, true);   // wantDuplicates = true (same address, new payload each wake)
  scan->setMaxResults(0);                  // don't store results, callback only
  scan->setActiveScan(false);              // passive; nodes are advertisement-only
  scan->setInterval(100);                  // 62.5 ms
  scan->setWindow(99);
  bool ok = scan->start(0, false);         // 0 = scan forever
  Serial.printf("[hub] scan start: %s\n", ok ? "OK" : "FAIL");
}

void loop() {
  if (millis() - lastSummary >= 10000) {
    lastSummary = millis();
    Serial.println("---- summary ----------------------------------------------");
    Serial.printf("scan stats: total_ads=%lu with_mfg=%lu wrong_len=%lu wrong_company=%lu ours=%lu\n",
                  (unsigned long)dTotal, (unsigned long)dMfg, (unsigned long)dBadLen,
                  (unsigned long)dBadCompany, (unsigned long)dOurs);
    for (int id = 1; id <= 5; id++) {
      NodeStat& s = stats[id];
      if (!s.seen && s.bad == 0) continue;
      Serial.printf("%-12s last=%lu new=%lu dup=%lu gaps=%lu replay=%lu bad=%lu rssi=%d age=%lus\n",
                    PPE_NODE_NAMES[id], (unsigned long)s.lastCtr, (unsigned long)s.newPkts,
                    (unsigned long)s.dups, (unsigned long)s.gaps, (unsigned long)s.replays,
                    (unsigned long)s.bad, s.rssi, (unsigned long)((millis() - s.lastMs) / 1000));
    }
    Serial.println("-----------------------------------------------------------");
  }
  delay(50);
}
