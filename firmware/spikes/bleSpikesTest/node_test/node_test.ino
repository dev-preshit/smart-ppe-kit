// S0 BLE spike - NODE test sketch (ESP32-C3)
// Board: "ESP32C3 Dev Module", USB CDC On Boot: Enabled
// Cycle: bump NVS counter -> sign -> advertise (non-connectable) -> sleep
#include <NimBLEDevice.h>
#include <Preferences.h>
#include "ppe_proto.h"

#define NODE_ID          5      // 1 LEFT_BOOT, 2 RIGHT_BOOT, 3 LEFT_GLOVE, 4 RIGHT_GLOVE, 5 HELMET
#define SLEEP_SECONDS    10     // spike value; real heartbeat is 300
#define ADV_WINDOW_MS    1000   // how long each cycle advertises
#define BAD_MAC_EVERY    10     // every Nth counter gets a corrupted MAC (hub must reject). 0 = off

// 1 = stay awake and loop, so the USB serial monitor stays connected (use this first)
// 0 = real deep sleep (USB port drops on every sleep, so the monitor will look dead; watch the HUB instead)
#define DEBUG_NO_SLEEP   0

Preferences prefs;

static void runCycle() {
  prefs.begin("ppe", false);
  uint32_t counter = prefs.getUInt("ctr", 0) + 1;
  prefs.putUInt("ctr", counter);
  prefs.end();

  PpePacket pkt;
  pkt.company = PPE_COMPANY_ID;
  pkt.ver     = PPE_PROTO_VER;
  pkt.nodeId  = NODE_ID;
  pkt.type    = PPE_HEARTBEAT;
  pkt.flags   = 0x01;           // pretend "worn"
  pkt.counter = counter;
  ppeComputeMac(pkt, pkt.mac);

  bool corrupted = false;
  if (BAD_MAC_EVERY > 0 && (counter % BAD_MAC_EVERY) == 0) {
    pkt.mac[0] ^= 0xFF;
    corrupted = true;
  }

  NimBLEDevice::init("");
  NimBLEDevice::setPower(3);    // dBm

  NimBLEAdvertisementData adData;
  adData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  adData.setManufacturerData((const uint8_t*)&pkt, sizeof(pkt));

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setAdvertisementData(adData);
  adv->enableScanResponse(false);
  adv->setConnectableMode(BLE_GAP_CONN_MODE_NON);   // non-connectable (2.5.1 API)
  adv->setMinInterval(0x20);    // 20 ms (0.625 ms units)
  adv->setMaxInterval(0x30);    // 30 ms

  bool ok = adv->start(ADV_WINDOW_MS);
  Serial.printf("[node %u %s] ctr=%lu adv_start=%s%s\n",
                NODE_ID, PPE_NODE_NAMES[NODE_ID], (unsigned long)counter,
                ok ? "OK" : "FAIL", corrupted ? " (MAC CORRUPTED ON PURPOSE)" : "");

  delay(ADV_WINDOW_MS + 100);
  adv->stop();
  NimBLEDevice::deinit(true);
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < (DEBUG_NO_SLEEP ? 5000 : 3000)) delay(10);  // wait for monitor
  delay(300);
  Serial.printf("\n[node %u] boot, DEBUG_NO_SLEEP=%d\n", NODE_ID, DEBUG_NO_SLEEP);

  runCycle();

#if !DEBUG_NO_SLEEP
  Serial.printf("[node %u] deep sleep %d s\n", NODE_ID, SLEEP_SECONDS);
  Serial.flush();
  esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_SECONDS * 1000000ULL);
  esp_deep_sleep_start();
#endif
}

void loop() {
#if DEBUG_NO_SLEEP
  delay(SLEEP_SECONDS * 1000UL);
  runCycle();
#endif
}
