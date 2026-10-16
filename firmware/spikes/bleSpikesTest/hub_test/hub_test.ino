// S0 BLE spike - HUB test sketch (ESP32-S3), adapted to the S1 shared library (ppe_common).
// Board: "ESP32S3 Dev Module", USB CDC On Boot: Enabled
// Needs build_opt.h next to this sketch (one line: -DPPE_TEST_HOOKS) for the public test key.
// Continuous passive scan; runs the S1 receive filter, tracks counters per node, prints a summary every 10 s.
#include <NimBLEDevice.h>
#include <ppe_common.h>
#include <ppe_test_key.h>

#define HUB_VEST_ID 0x01   // must equal VEST_ID in node_test

static const char* const NODE_NAMES[] = {
  "?", "LEFT_BOOT", "RIGHT_BOOT", "LEFT_GLOVE", "RIGHT_GLOVE", "HELMET"
};

struct NodeStat {
  bool     seen = false;
  uint32_t lastCtr = 0;
  uint32_t newPkts = 0;     // distinct counters accepted
  uint32_t dups = 0;        // repeats of same counter within a burst
  uint32_t gaps = 0;        // counters skipped (= missed wake bursts)
  uint32_t replays = 0;     // counter went backwards
  uint32_t bad = 0;         // bad-HMAC counters (distinct)
  uint32_t lastBadCtr = 0xFFFFFFFF;
  int      rssi = 0;
  uint32_t lastMs = 0;
};
NodeStat stats[PPE_NODE_COUNT + 1];   // index = node_id 1..5

// diagnostics: is scanning working at all, and why are packets dropped?
volatile uint32_t dTotal = 0, dMfg = 0, dOurs = 0;
volatile uint32_t dDropLen = 0, dDropVer = 0, dDropVest = 0, dDropNode = 0, dDropState = 0;

class ScanCB : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* d) override {
    dTotal++;
    if (!d->haveManufacturerData()) return;
    dMfg++;
    std::string md = d->getManufacturerData();      // company id (2) + 17-byte payload
    if (md.size() != PPE_ADV_LEN - 2u) { dDropLen++; return; }

    // Rebuild the 21-byte AD data (len, type, company, payload) for the shared filter.
    uint8_t adv[PPE_ADV_LEN];
    adv[0] = PPE_AD_LEN_BYTE;
    adv[1] = PPE_AD_TYPE_MSD;
    memcpy(adv + 2, md.data(), md.size());

    bool nonconn = !d->isConnectable() && !d->isScannable();   // spike: ADV_NONCONN_IND check
    ppe_packet_t pkt = {};
    ppe_rx_result_t res = ppe_rx_filter(nonconn, adv, sizeof(adv), HUB_VEST_ID, PPE_TEST_KEY, &pkt);
    int rssi = d->getRSSI();

    switch (res) {
      case PPE_RX_OK: break;
      case PPE_RX_DROP_LEN:   dDropLen++;   return;
      case PPE_RX_DROP_VER:   dDropVer++;   return;
      case PPE_RX_DROP_VEST:  dDropVest++;  return;
      case PPE_RX_DROP_NODE:  dDropNode++;  return;
      case PPE_RX_DROP_STATE: dDropState++; return;
      case PPE_RX_HMAC_FAIL: {
        NodeStat& b = stats[pkt.node_id];             // node_id is trustworthy only for HMAC_FAIL
        dOurs++;
        if (b.lastBadCtr != pkt.counter) {
          b.lastBadCtr = pkt.counter;
          b.bad++;
          Serial.printf("[BAD HMAC] %s ctr=%lu rssi=%d  -> rejected\n",
                        NODE_NAMES[pkt.node_id], (unsigned long)pkt.counter, rssi);
        }
        return;
      }
    }
    dOurs++;

    NodeStat& s = stats[pkt.node_id];
    s.rssi = rssi;
    s.lastMs = millis();

    if (!s.seen) {
      s.seen = true; s.lastCtr = pkt.counter; s.newPkts++;
      Serial.printf("[FIRST   ] %s ctr=%lu rssi=%d\n",
                    NODE_NAMES[pkt.node_id], (unsigned long)pkt.counter, rssi);
      return;
    }

    uint32_t missed = 0;
    switch (ppe_counter_check(s.lastCtr, pkt.counter, &missed)) {
      case PPE_CTR_DUPLICATE: s.dups++; return;
      case PPE_CTR_REPLAY:
        s.replays++;
        Serial.printf("[REPLAY  ] %s ctr=%lu (last=%lu) -> rejected\n",
                      NODE_NAMES[pkt.node_id], (unsigned long)pkt.counter, (unsigned long)s.lastCtr);
        return;
      case PPE_CTR_ACCEPT:
        s.gaps += missed;
        s.lastCtr = pkt.counter;
        s.newPkts++;
        Serial.printf("[OK      ] %s ctr=%lu rssi=%d%s\n",
                      NODE_NAMES[pkt.node_id], (unsigned long)pkt.counter, rssi,
                      missed ? "  <-- gap before this" : "");
        return;
    }
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
    Serial.printf("scan stats: total_ads=%lu with_mfg=%lu ours=%lu drop_len=%lu drop_ver=%lu drop_vest=%lu drop_node=%lu drop_state=%lu\n",
                  (unsigned long)dTotal, (unsigned long)dMfg, (unsigned long)dOurs,
                  (unsigned long)dDropLen, (unsigned long)dDropVer, (unsigned long)dDropVest,
                  (unsigned long)dDropNode, (unsigned long)dDropState);
    for (int id = 1; id <= (int)PPE_NODE_COUNT; id++) {
      NodeStat& s = stats[id];
      if (!s.seen && s.bad == 0) continue;
      Serial.printf("%-12s last=%lu new=%lu dup=%lu gaps=%lu replay=%lu bad=%lu rssi=%d age=%lus\n",
                    NODE_NAMES[id], (unsigned long)s.lastCtr, (unsigned long)s.newPkts,
                    (unsigned long)s.dups, (unsigned long)s.gaps, (unsigned long)s.replays,
                    (unsigned long)s.bad, s.rssi, (unsigned long)((millis() - s.lastMs) / 1000));
    }
    Serial.println("-----------------------------------------------------------");
  }
  delay(50);
}
