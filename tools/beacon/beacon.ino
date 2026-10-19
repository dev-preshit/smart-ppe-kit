// S3 bench tool: reference beacon (spare ESP32-C3). TEST TOOL, never part of a release.
// Board: "ESP32C3 Dev Module", USB CDC On Boot: Enabled. build_opt.h next to this file holds -DPPE_TEST_HOOKS.
// Sends ONE valid, signed single-copy packet (one burst) every BEACON_PERIOD_S as node BEACON_NODE_ID, vest 0x01,
// state WORN. The counter uses the S1 node counter with an NVS block, so it never goes backwards across resets
// (the hub would reject a lower counter as a replay). Used by TC-20, TC-21 and the Wi-Fi + BLE recheck.
// Do NOT inject node 5 packets with tools/inject.py while the beacon runs; use nodes 1-4 for injection.
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <ppe_common.h>
#include <ppe_test_key.h>

#define BEACON_NODE_ID   5        // HELMET slot, kept apart from the injector's nodes 1-4
#define VEST_ID          0x01     // same test vest id as the hub bench build
#define BEACON_PERIOD_S  10
#define ADV_BURST_MS     100u

class PrefsStore : public PpeCounterStore {
 public:
  bool read_ceiling(uint32_t* v) override {
    Preferences p; if (!p.begin("ppebeacon", false)) return false;
    *v = p.getUInt("ceil", 0); p.end(); return true;
  }
  bool write_ceiling(uint32_t v) override {
    Preferences p; if (!p.begin("ppebeacon", false)) return false;
    size_t n = p.putUInt("ceil", v); p.end(); return n == sizeof(uint32_t);
  }
};
static PrefsStore store;
static PpeNodeCounter counter(&store);
static NimBLEAdvertising* advertising = nullptr;
static bool bootFlagPending = true;
static uint32_t nextAt = 0;

static void sendOne() {
  uint32_t c = 0;
  ppe_ctr_status_t st = counter.next(&c);
  if (st != PPE_CTR_OK) { Serial.printf("BEACON counter fault status=%d, not transmitting\n", (int)st); return; }
  ppe_packet_t pk = {};
  pk.flags = ppe_flags_make(PPE_HEARTBEAT, bootFlagPending, false, false);
  pk.vest_id = VEST_ID; pk.node_id = BEACON_NODE_ID; pk.state = PPE_WORN; pk.battery = PPE_BATT_NA; pk.counter = c;
  bootFlagPending = false;
  uint8_t payload[PPE_PAYLOAD_LEN];
  ppe_encode_payload(pk, payload);
  if (!ppe_sign_payload(PPE_TEST_KEY, payload)) { Serial.println("BEACON sign failed"); return; }
  uint8_t adv[PPE_ADV_LEN];
  ppe_make_adv(payload, adv);
  NimBLEAdvertisementData ad;
  ad.addData(adv, PPE_ADV_LEN);
  advertising->setAdvertisementData(ad);
  advertising->enableScanResponse(false);
  advertising->setConnectableMode(BLE_GAP_CONN_MODE_NON);
  advertising->setMinInterval(0x20);
  advertising->setMaxInterval(0x30);
  bool ok = advertising->start(ADV_BURST_MS);
  Serial.printf("BEACON t_ms=%lu node=%u ctr=%lu adv=%s\n", (unsigned long)millis(), BEACON_NODE_ID, (unsigned long)c, ok ? "ok" : "FAIL");
  delay(ADV_BURST_MS + 10);
  advertising->stop();
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);
  delay(200);
  ppe_ctr_status_t st = counter.cold_boot();      // reserve a new counter block in NVS first (spec 6.1)
  if (st != PPE_CTR_OK) { Serial.printf("BEACON cold_boot failed status=%d, not transmitting\n", (int)st); return; }
  NimBLEDevice::init("");
  NimBLEDevice::setPower(3);
  advertising = NimBLEDevice::getAdvertising();
  Serial.printf("BEACON ready, node %u every %d s\n", BEACON_NODE_ID, BEACON_PERIOD_S);
  nextAt = millis();
}

void loop() {
  if (advertising && (int32_t)(millis() - nextAt) >= 0) {
    nextAt += BEACON_PERIOD_S * 1000UL;
    sendOne();
  }
  delay(5);
}
