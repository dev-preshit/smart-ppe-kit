// Smart PPE Kit hub (ESP32-S3). Sprint S3: BLE receive path and persistence.
// Board: "ESP32S3 Dev Module", USB CDC On Boot: Enabled. Library: ppe_common (junction, see common/README.md),
// NimBLE-Arduino 2.5.1. Release build = this folder as it is (no build_opt.h): it refuses to run because
// provisioning (vest id + key) is sprint S6. Bench build = copy build_opt.h.bench to build_opt.h (git-ignored).
#include <Arduino.h>
#include "hub_nvs.h"
#include "hub_ble.h"
#include "hub_motor_out.h"
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
  hub_motor_early_init();            // B-01: the motor pin goes LOW before anything else can take time
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
  uint32_t forces = 0;
#ifdef PPE_TEST_HOOKS
  forces = hub_take_boot_forces();   // TC-30: a one-shot fault chosen before the last restart
  if (forces & HUB_FORCE_NO_KEY) { key = nullptr; Serial.println("BOOT PPE_HOOK fault key: this start treats the key as missing (one shot)"); }
  hub_nvs_force_params_fail((forces & HUB_FORCE_PARAMS) != 0);
#endif
  HubStartResult sr = hub_startup_check(vest, key != nullptr);
  if (sr == HUB_START_BAD_VEST) {    // fail safe: no scan, no accept (brief story 4)
    Serial.printf("REFUSE start-up: invalid hub vest id (valid is 0x01..0xFE) (vest id 0x%02X)\n", vest);
    return;
  }
  // A missing key is not a refusal any more: B-04 runs the hub with accept disabled and the KEY_FAULT pattern (S5, TC-30).
  const bool nvs_open = g_store.begin();
  if (!nvs_open) Serial.println("ERROR NVS open failed: registry unreadable, accept stays disabled");
  bool params_ok = true, found = false;
  uint32_t hb = 0, mg = 0;
  if (nvs_open) {
    params_ok = g_store.read_params(&hb, &mg, &found);
    if (!params_ok) params_ok = g_store.read_params(&hb, &mg, &found);      // X-03: retry once, then continue with the defaults
  }
  HubStore* store = &g_store;
#ifdef PPE_TEST_HOOKS
  if (forces & HUB_FORCE_REGISTRY) { store = nullptr; Serial.println("BOOT PPE_HOOK fault registry: this start treats the registry as unreadable (one shot)"); }
#endif
  if (!hub_persist_start(&g_store)) Serial.println("ERROR persist task not started");
  g_running = hub_ble_start(vest, key, store, params_ok);
  if (g_running && params_ok && found) hub_apply_params(hb, mg);
#ifdef PPE_S3_WIFI_RECHECK
  if (g_running) hub_wifi_recheck_start();   // after the scan runs, so the recheck sees BLE and Wi-Fi together
#endif
  Serial.printf("HUB running=%d\n", g_running);
}

#ifdef PPE_TEST_HOOKS
// Serial test hooks: inject <hex> | stall <n> | dump | time [on|off] | rxlog on|off | set margin <s> | set heartbeat <s> | motor | marker on|off | pulse <on> <off> <n> | fault <kind>  (one command per line)
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
    case HUB_HOOK_SET_MARGIN: hub_hook_set_margin(c.val); break;
    case HUB_HOOK_SET_HEARTBEAT: hub_hook_set_heartbeat(c.val); break;
    case HUB_HOOK_MOTOR: hub_hook_motor(); break;
    case HUB_HOOK_FAULT: hub_hook_fault((uint8_t)c.val); break;
    case HUB_HOOK_MARKER_ON: hub_hook_marker(true); break;
    case HUB_HOOK_MARKER_OFF: hub_hook_marker(false); break;
    case HUB_HOOK_PULSE: hub_hook_pulse(c.val, c.val2, c.val3); break;
    default: Serial.println("PPE_HOOK error: commands are inject <hex>, stall <1-9>, dump, time [on|off], rxlog on|off, set margin <s>, set heartbeat <s>, motor, marker on|off, pulse <on_ms> <off_ms> <count>, fault wdt|nvs-read|nvs-write|registry|key|guard"); break;
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
