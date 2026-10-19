// Smart PPE Kit hub (ESP32-S3). Sprint S3: BLE receive path and persistence.
// Board: "ESP32S3 Dev Module", USB CDC On Boot: Enabled. Library: ppe_common (junction, see common/README.md),
// NimBLE-Arduino 2.5.1. Release build = this folder as it is (no build_opt.h): it refuses to run because
// provisioning (vest id + key) is sprint S6. Bench build = copy build_opt.h.bench to build_opt.h (git-ignored).
#include <Arduino.h>
#include "hub_nvs.h"
#include "hub_ble.h"
#include "src/hub_pipeline.h"
#include "hub_wifi_recheck.h"
#ifdef PPE_TEST_HOOKS
#include "src/hub_hooks.h"
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
#ifdef PPE_S3_WIFI_RECHECK
  if (g_running) hub_wifi_recheck_start();   // after the scan runs, so the recheck sees BLE and Wi-Fi together
#endif
  Serial.printf("HUB running=%d\n", g_running);
}

#ifdef PPE_TEST_HOOKS
// Serial test hooks: inject <hex> | stall <n> | dump | time [on|off] | rxlog on|off  (one command per line)
static char g_line[256];
static size_t g_line_len = 0;

static void run_hook(const char* line) {
  HubHookCmd c = hub_hook_parse(line);
  switch (c.type) {
    case HUB_HOOK_NONE: break;
    case HUB_HOOK_INJECT:
      Serial.printf("PPE_HOOK inject len=%u %s\n", c.len, hub_hook_inject(c.data, c.len) ? "queued" : "REFUSED(queue full)");
      break;
    case HUB_HOOK_STALL: hub_hook_stall(c.n); break;
    case HUB_HOOK_DUMP: hub_hook_dump(); break;
    case HUB_HOOK_TIME_NOW: hub_hook_time_now(); break;
    case HUB_HOOK_TIME_ON: hub_hook_set_time(true); break;
    case HUB_HOOK_TIME_OFF: hub_hook_set_time(false); break;
    case HUB_HOOK_RXLOG_ON: hub_hook_set_rxlog(true); break;
    case HUB_HOOK_RXLOG_OFF: hub_hook_set_rxlog(false); break;
    default: Serial.println("PPE_HOOK error: commands are inject <hex>, stall <1-9>, dump, time [on|off], rxlog on|off"); break;
  }
}

static void poll_hooks() {
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n') { g_line[g_line_len] = 0; if (g_running) run_hook(g_line); g_line_len = 0; }
    else if (g_line_len < sizeof g_line - 1) g_line[g_line_len++] = ch;
    else g_line_len = 0;                      // overlong line: drop it
  }
}
#endif

void loop() {
#ifdef PPE_TEST_HOOKS
  poll_hooks();
#endif
  if (!g_running) {
    if (millis() - g_last_refuse_print > 5000) {
      g_last_refuse_print = millis();
      Serial.println("HUB not running (see REFUSE / ERROR above)");
    }
  }
  delay(10);
}
