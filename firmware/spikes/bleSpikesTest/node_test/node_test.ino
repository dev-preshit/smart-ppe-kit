// S0 BLE spike - NODE test sketch (ESP32-C3), adapted to the S1 shared library (ppe_common).
// Board: "ESP32C3 Dev Module", USB CDC On Boot: Enabled
// Needs build_opt.h next to this sketch (one line: -DPPE_TEST_HOOKS) for the public test key.
// Cycle: next counter (S1 PpeNodeCounter) -> sign -> advertise (non-connectable) -> sleep
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <ppe_common.h>
#include <ppe_test_key.h>

#define NODE_ID          5      // 1 LEFT_BOOT, 2 RIGHT_BOOT, 3 LEFT_GLOVE, 4 RIGHT_GLOVE, 5 HELMET
#define VEST_ID          0x01   // test vest id; the hub sketch must use the same value
#define SLEEP_SECONDS    10     // spike value; real heartbeat is 360
#define ADV_WINDOW_MS    1000   // how long each cycle advertises
#define BAD_MAC_EVERY    10     // every Nth counter gets a corrupted tag (hub must reject). 0 = off

// 1 = stay awake and loop, so the USB serial monitor stays connected (use this first)
// 0 = real deep sleep (USB port drops on every sleep, so the monitor will look dead; watch the HUB instead)
#define DEBUG_NO_SLEEP   0

static const char* const NODE_NAMES[] = {
  "?", "LEFT_BOOT", "RIGHT_BOOT", "LEFT_GLOVE", "RIGHT_GLOVE", "HELMET"
};

// NVS-backed ceiling store for the S1 counter block (spec 6.1).
// Opened read/write even for reads so the namespace exists on first boot.
class PrefsStore : public PpeCounterStore {
 public:
  bool read_ceiling(uint32_t* v) override {
    Preferences p;
    if (!p.begin("ppe", false)) return false;
    *v = p.getUInt("ceil", 0);
    p.end();
    return true;
  }
  bool write_ceiling(uint32_t v) override {
    Preferences p;
    if (!p.begin("ppe", false)) return false;
    size_t n = p.putUInt("ceil", v);
    p.end();
    return n == sizeof(uint32_t);
  }
};

static PrefsStore store;
static PpeNodeCounter counter(&store);

// Kept across deep sleep (RTC memory) so a wake does not touch NVS (spec 6.1, N-03).
RTC_DATA_ATTR static uint32_t rtcCounter = 0;
RTC_DATA_ATTR static uint32_t rtcCeiling = 0;
RTC_DATA_ATTR static bool     rtcValid   = false;

static bool bootFlagPending = false;   // BOOT flag on the first packet after a real boot

static void runCycle() {
  uint32_t c = 0;
  ppe_ctr_status_t st = counter.next(&c);
  if (st != PPE_CTR_OK) {
    Serial.printf("[node %u] NODE_FAULT counter status=%d, NOT transmitting\n", NODE_ID, (int)st);
    return;
  }
  rtcCounter = counter.counter();
  rtcCeiling = counter.ceiling();
  rtcValid   = true;

  ppe_packet_t pk = {};
  pk.flags   = ppe_flags_make(PPE_HEARTBEAT, bootFlagPending, false, false);
  pk.vest_id = VEST_ID;
  pk.node_id = NODE_ID;
  pk.state   = PPE_WORN;          // pretend "worn"
  pk.battery = PPE_BATT_NA;       // no battery reading in the spike
  pk.counter = c;
  bootFlagPending = false;

  uint8_t payload[PPE_PAYLOAD_LEN];
  ppe_encode_payload(pk, payload);
  if (!ppe_sign_payload(PPE_TEST_KEY, payload)) {
    Serial.printf("[node %u] sign failed, NOT transmitting\n", NODE_ID);
    return;
  }

  bool corrupted = false;
  if (BAD_MAC_EVERY > 0 && (c % BAD_MAC_EVERY) == 0) {
    payload[PPE_OFF_HMAC] ^= 0xFF;
    corrupted = true;
  }

  uint8_t adv[PPE_ADV_LEN];       // 14 FF FF FF + 17-byte payload (spec 2: MSD only, no Flags AD)
  ppe_make_adv(payload, adv);

  NimBLEDevice::init("");
  NimBLEDevice::setPower(3);      // dBm

  NimBLEAdvertisementData adData;
  adData.addData(adv, PPE_ADV_LEN);

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->setAdvertisementData(adData);
  advertising->enableScanResponse(false);
  advertising->setConnectableMode(BLE_GAP_CONN_MODE_NON);   // non-connectable (2.5.1 API)
  advertising->setMinInterval(0x20);    // 20 ms (0.625 ms units)
  advertising->setMaxInterval(0x30);    // 30 ms

  bool ok = advertising->start(ADV_WINDOW_MS);
  Serial.printf("[node %u %s] ctr=%lu adv_start=%s%s\n",
                NODE_ID, NODE_NAMES[NODE_ID], (unsigned long)c,
                ok ? "OK" : "FAIL", corrupted ? " (TAG CORRUPTED ON PURPOSE)" : "");

  delay(ADV_WINDOW_MS + 100);
  advertising->stop();
  NimBLEDevice::deinit(true);
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < (DEBUG_NO_SLEEP ? 5000 : 3000)) delay(10);  // wait for monitor
  delay(300);
  Serial.printf("\n[node %u] boot, DEBUG_NO_SLEEP=%d\n", NODE_ID, DEBUG_NO_SLEEP);

  if (esp_reset_reason() == ESP_RST_DEEPSLEEP && rtcValid) {
    counter.resume(rtcCounter, rtcCeiling);          // deep-sleep wake: no NVS access
  } else {
    ppe_ctr_status_t st = counter.cold_boot();       // real boot: reserve a new block in NVS first
    if (st != PPE_CTR_OK) {
      Serial.printf("[node %u] cold_boot failed status=%d, NOT transmitting\n", NODE_ID, (int)st);
      return;
    }
    bootFlagPending = true;
  }

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
