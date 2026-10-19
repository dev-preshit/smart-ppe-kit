// Smart PPE Kit hub (ESP32-S3). Sprint S3: BLE receive path and persistence.
// Board: "ESP32S3 Dev Module", USB CDC On Boot: Enabled. Library: ppe_common (junction, see common/README.md),
// NimBLE-Arduino 2.5.1. Release build = this folder as it is (no build_opt.h): it refuses to run because
// provisioning (vest id + key) is sprint S6. Bench build = copy build_opt.h.bench to build_opt.h (git-ignored).
#include <Arduino.h>
#include "hub_nvs.h"
#include "hub_ble.h"
#include "src/hub_pipeline.h"
#ifdef PPE_TEST_HOOKS
#include <ppe_test_key.h>
#ifndef HUB_TEST_VEST_ID
#define HUB_TEST_VEST_ID 0x01      // override with -DHUB_TEST_VEST_ID=0xFF to test the start-up refusal
#endif
#endif

static HubNvsStore g_store;
static bool g_running = false;
static uint32_t g_last_refuse_print = 0;

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);
  delay(300);
  Serial.println("\nHUB boot (S3 receive path)");

  uint8_t vest = 0;
  const uint8_t* key = nullptr;
#ifdef PPE_TEST_HOOKS
  vest = HUB_TEST_VEST_ID;           // test constants only; real provisioning is S6, the real key is never in the repo
  key = PPE_TEST_KEY;
#endif
  HubStartResult sr = hub_startup_check(vest, key != nullptr);
  if (sr != HUB_START_OK) {          // fail safe: no scan, no accept (brief story 4)
    Serial.printf("REFUSE start-up: %s (vest id 0x%02X)\n",
                  sr == HUB_START_BAD_VEST ? "invalid hub vest id (valid is 0x01..0xFE)" : "no key provisioned", vest);
    return;
  }
  if (!g_store.begin()) Serial.println("ERROR NVS open failed: registry unreadable, accept stays disabled");
  if (!hub_persist_start(&g_store)) Serial.println("ERROR persist task not started");
  g_running = hub_ble_start(vest, key, &g_store);
  Serial.printf("HUB running=%d\n", g_running);
}

void loop() {
  if (!g_running) {
    if (millis() - g_last_refuse_print > 5000) {
      g_last_refuse_print = millis();
      Serial.println("HUB not running (see REFUSE / ERROR above)");
    }
  }
  delay(10);
}
