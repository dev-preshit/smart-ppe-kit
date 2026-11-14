// node_sense.ino - node firmware. S2: sensing and confirm logic. S4 increment 2: radio, identity from NVS, counter block in NVS,
// event and heartbeat sequences (5 copies, 500 ms). No deep sleep yet (S4 increment 3), no battery ADC yet (battery = 0xFF).
// Board: ESP32C3 Dev Module, USB CDC On Boot: Enabled. Library: ppe_common (linked from common/).
// Build flags (all optional):  -DPPE_TEST_HOOKS  -DPPE_BUILD_NODE_TYPE=0|1|2 (0 FOOT, 1 GLOVE, 2 HELMET)
//   -DPPE_CONTACT_CLOSED_LEVEL=0|1 (pin level when a contact is closed, default 0 = closed pulls to GND, B-02)
// A node with no valid identity in NVS (node_id 1..5, vest_id 1..254, key) is UNPROVISIONED: the radio is never started (N-01).
#include <Arduino.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <ppe_node.h>        // first: it brings ppe_proto.h, whose constants the other ppe headers then reuse
#include <ppe_sense.h>
#include <ppe_settle.h>
#include <ppe_confirm.h>
#include <ppe_config.h>
#include <ppe_log.h>
#include <ppe_counter.h>
#include <ppe_tx.h>
#ifdef PPE_TEST_HOOKS
#include <ppe_hooks.h>
#include <ppe_test_key.h>
#endif

// Numeric on purpose: enum names are not visible to the preprocessor.
#ifndef PPE_BUILD_NODE_TYPE
#define PPE_BUILD_NODE_TYPE 0
#endif
#ifndef PPE_CONTACT_CLOSED_LEVEL
#define PPE_CONTACT_CLOSED_LEVEL 0
#endif

// Pin map (B-01). GPIO4 is reserved for the S4 FSR wake comparator and is not used here (A-18: no comparator).
static const uint8_t PIN_FSR = 0;        // ADC1_CH0
static const uint8_t PIN_CONTACT_A = 1;  // glove strap contact, or helmet crown switch
static const uint8_t PIN_CONTACT_B = 3;  // helmet chin-strap contact
static const uint8_t PIN_MARKER = 10;    // optional GPIO marker pulse (test builds)
static const uint32_t POLL_MS = 50;      // AS-03

static const char* const NVS_NS = "ppenode";
enum { RUN_UNPROVISIONED = 0, RUN_ACTIVE, RUN_FAULT };
enum { FAULT_NONE = 0, FAULT_LIMIT, FAULT_NVS, FAULT_BLE, FAULT_CRYPTO };
static const char* const FAULT_NAME[] = {"none", "counter-limit", "nvs-block-write", "ble-start", "crypto"};

static ppe_cfg_t     cfg;
static ppe_fsr_t     fsr;
static ppe_settle_t  settle;
static ppe_confirm_t confirm;
static ppe_raw_t     last_raw;
static uint32_t      next_poll;

static Preferences   prefs;
static ppe_ident_t   ident;
static ppe_tx_t      txs;
static NimBLEAdvertising* adv = nullptr;
static uint8_t       g_run = RUN_UNPROVISIONED;
static uint8_t       g_fault = FAULT_NONE;
static bool          g_radio_up = false;
static uint8_t       g_adv[PPE_ADV_LEN];
static uint32_t      g_tx_seq = 0;

#ifdef PPE_TEST_HOOKS
#ifndef PPE_TEST_ADC_BOOT
#define PPE_TEST_ADC_BOOT -1          // -DPPE_TEST_ADC_BOOT=800 gives a power-on reading of 800 (TC-03 step 2)
#endif
static long g_adc_inject = PPE_TEST_ADC_BOOT;   // -1 = use the real ADC
static bool g_adc_fail = false;
static bool g_marker_on = true;
static bool g_nvslog = false;
static bool g_fail_nvs_write = false;
static uint8_t g_fail_ble_starts = 0;
static uint8_t g_skip_n = 0;
static bool g_seq_suppressed = false;
static char g_line[48];
static uint8_t g_len = 0;

static void mark(const char* name) { char b[48]; ppe_mark_fmt(b, sizeof b, name, micros()); Serial.println(b); }
static void mark_us(const char* name, uint32_t us) { char b[48]; ppe_mark_fmt(b, sizeof b, name, us); Serial.println(b); }
static void mark_raw(const char* name, ppe_raw_t r) { char b[64]; ppe_mark_fmt_raw(b, sizeof b, name, micros(), r); Serial.println(b); }
static void pulse() { if (g_marker_on) { digitalWrite(PIN_MARKER, HIGH); delayMicroseconds(100); digitalWrite(PIN_MARKER, LOW); } }
static void print_cfg() { char b[96]; ppe_cfg_fmt(b, sizeof b, (ppe_node_type_t)PPE_BUILD_NODE_TYPE, &cfg); Serial.println(b); }
#else
static void mark(const char*) {}
static void mark_us(const char*, uint32_t) {}
static void mark_raw(const char*, ppe_raw_t) {}
static void pulse() {}
#endif

// ---- NVS: counter block (key "ceil") behind the interface the host tests fake (FR-11, T-02)
class NodeStore : public PpeCounterStore {
 public:
  bool read_ceiling(uint32_t* v) override {
    if (!prefs.isKey("ceil")) { *v = 0; return true; }       // fresh node: baseline 0
    *v = prefs.getUInt("ceil", 0);
    return true;
  }
  bool write_ceiling(uint32_t v) override {
#ifdef PPE_TEST_HOOKS
    if (g_fail_nvs_write) {                                   // hook: fault nvs-write, the next block write fails once
      g_fail_nvs_write = false;
      if (g_nvslog) Serial.printf("NVS write key=ceil value=%lu FAILED (fault hook)\n", (unsigned long)v);
      return false;
    }
#endif
    bool ok = prefs.putUInt("ceil", v) == sizeof(uint32_t);
#ifdef PPE_TEST_HOOKS
    if (g_nvslog) Serial.printf("NVS write key=ceil value=%lu ok=%d\n", (unsigned long)v, (int)ok);
#endif
    return ok;
  }
};
static NodeStore   store;
static PpeNodeCounter counter(&store);

static void load_ident() {
  ppe_ident_clear(&ident);
  ident.node_id = prefs.getUChar("node", 0);
  ident.vest_id = prefs.getUChar("vest", 0);
  ident.has_key = prefs.getBytes("key", ident.key, PPE_KEY_LEN) == PPE_KEY_LEN;     // the key is never printed or logged
}

static void enter_fault(uint8_t why) {
  g_fault = why;
  g_run = RUN_FAULT;
  ppe_tx_abort(&txs);
  if (adv) adv->stop();
  Serial.printf("FAULT %s: the node stops transmitting until a reset\n", FAULT_NAME[why]);
}

// ---- radio. The radio rule (S3 finding): connectable NON, discoverable NON, no scan response and the intervals are set ONCE here,
// before any data is set. Later only setAdvertisementData() and start()/stop() are called, otherwise NimBLE sends ADV_SCAN_IND
// or appends 02 01 04 (24 bytes) and the hub drops the packet as DROP_LEN.
static void radio_init() {
  NimBLEDevice::init("");
  adv = NimBLEDevice::getAdvertising();
  adv->setConnectableMode(BLE_GAP_CONN_MODE_NON);
  adv->setDiscoverableMode(BLE_GAP_DISC_MODE_NON);
  adv->enableScanResponse(false);
  adv->setMinInterval(0x20);              // 20 ms
  adv->setMaxInterval(0x30);              // 30 ms
  g_radio_up = true;
}

static bool start_copy_once() {
#ifdef PPE_TEST_HOOKS
  if (g_seq_suppressed) return true;                          // hook: skiptx, the copies are not sent
  if (g_fail_ble_starts > 0) { g_fail_ble_starts--; return false; }   // hook: fault ble
#endif
  NimBLEAdvertisementData ad;
  ad.addData(g_adv, PPE_ADV_LEN);                             // raw bytes, nothing added by the library
  adv->setAdvertisementData(ad);
  return adv->start(PPE_ADV_BURST_MS_DEFAULT);
}

// T-07: retry the start once, a second failure is NODE_FAULT (N-31). The mark is taken just before the start (A-20).
static bool start_copy(uint8_t k) {
  uint32_t us = micros();
  bool ok = start_copy_once();
  if (!ok) { delay(5); us = micros(); ok = start_copy_once(); }
  if (!ok) return false;
  char name[8]; snprintf(name, sizeof name, "COPY%u", (unsigned)k);
  mark_us(name, us);
  if (k == 0) pulse();                                        // marker hook: the first copy start
  return true;
}

static void stop_copy() {
#ifdef PPE_TEST_HOOKS
  if (g_seq_suppressed) return;
#endif
  adv->stop();
}

// ---- transmit sequences (T-01 to T-07)
static void on_build(ppe_tx_act_t act, uint32_t now) {
  bool is_event = (act == PPE_TX_ACT_BUILD_EVENT);
  uint32_t ctr = 0;
  ppe_ctr_status_t st = counter.next(&ctr);                   // T-02 and T-03: counter + 1, block extended in NVS first when needed
  if (st != PPE_CTR_OK) { enter_fault(st == PPE_CTR_ERR_LIMIT ? FAULT_LIMIT : FAULT_NVS); return; }   // N-09, N-30
  ppe_raw_t pair = is_event ? txs.cur_event.pair : confirm.confirmed;
  bool boot = is_event && txs.cur_event.boot;
  uint8_t battery = PPE_BATT_NA;                              // no battery ADC yet (TC-13 later)
  if (!ppe_build_adv(&ident, is_event ? PPE_EVENT : PPE_HEARTBEAT, boot, pair.fault != 0, pair.state, battery, ctr, g_adv)) {
    enter_fault(FAULT_CRYPTO); return;
  }
  g_tx_seq++;
  ppe_tx_begin(&txs, now);
  ppe_hb_on_sequence_start(&confirm, now, cfg.heartbeat_s);   // N-27
  PPE_LOGI("node", "TX seq=%lu type=%s ctr=%lu state=%d boot=%d fault=%d", (unsigned long)g_tx_seq, is_event ? "event" : "heartbeat",
           (unsigned long)ctr, (int)pair.state, (int)boot, (int)pair.fault);
#ifdef PPE_TEST_HOOKS
  g_seq_suppressed = false;
  if (g_skip_n > 0) { g_skip_n--; g_seq_suppressed = true; Serial.println("TX suppressed (skiptx)"); }
#endif
  if (!start_copy(0)) { enter_fault(FAULT_BLE); return; }
}

static void tx_pump(uint32_t now) {
  if (ppe_hb_due(&confirm, now)) ppe_tx_request_heartbeat(&txs);       // N-26
  ppe_tx_act_t act = ppe_tx_step(&txs, now, PPE_ADV_BURST_MS_DEFAULT, PPE_RETX_PERIOD_MS);
  switch (act) {
    case PPE_TX_ACT_BUILD_EVENT:
    case PPE_TX_ACT_BUILD_HEARTBEAT: on_build(act, now); break;
    case PPE_TX_ACT_START_COPY: if (!start_copy(txs.copy)) enter_fault(FAULT_BLE); break;
    case PPE_TX_ACT_STOP_COPY: stop_copy(); break;
    default: break;
  }
}

static bool contact_closed(uint8_t pin) { return digitalRead(pin) == PPE_CONTACT_CLOSED_LEVEL; }

static bool read_fsr(uint32_t* count) {
#ifdef PPE_TEST_HOOKS
  if (g_adc_fail) return false;
  if (g_adc_inject >= 0) { *count = (uint32_t)g_adc_inject; return true; }
#endif
  uint16_t s[PPE_FSR_SAMPLES];
  for (int i = 0; i < PPE_FSR_SAMPLES; i++) {
    int v = analogRead(PIN_FSR);
    if (v < 0) return false;
    s[i] = (uint16_t)v;
  }
  *count = ppe_fsr_mean(s, PPE_FSR_SAMPLES);
  return true;
}

static ppe_raw_t sample_raw() {
#if PPE_BUILD_NODE_TYPE == 1
  return ppe_conv_glove(contact_closed(PIN_CONTACT_A));
#elif PPE_BUILD_NODE_TYPE == 2
  return ppe_conv_helmet(contact_closed(PIN_CONTACT_A), contact_closed(PIN_CONTACT_B));
#else
  uint32_t count = 0;
  bool ok = read_fsr(&count);
  ppe_raw_t r = ppe_conv_fsr(&fsr, ok, count);
  PPE_LOGD("fsr", "count=%lu ok=%d", (unsigned long)count, (int)ok);   // TC-03 bench log shows the filtered count (A-05)
  return r;
#endif
}

#ifdef PPE_TEST_HOOKS
static void print_node() {
  Serial.printf("NODE run=%s prov=%d node=%u vest=%u key=%s fault=%s radio=%d ctr=%lu ceil=%lu tx_state=%d skip=%u\n",
                g_run == RUN_ACTIVE ? "active" : (g_run == RUN_FAULT ? "fault" : "unprovisioned"), (int)ppe_ident_valid(&ident),
                (unsigned)ident.node_id, (unsigned)ident.vest_id, ident.has_key ? "yes" : "no", FAULT_NAME[g_fault], (int)g_radio_up,
                (unsigned long)counter.counter(), (unsigned long)counter.ceiling(), (int)txs.state, (unsigned)g_skip_n);
}

static void handle_line(const char* line) {
  ppe_hook_cmd_t h;
  switch (ppe_hook_parse(line, &h)) {
    case PPE_HOOK_EMPTY: break;
    case PPE_HOOK_ADC_SET:  g_adc_inject = h.arg; Serial.printf("OK adc %ld\n", h.arg); break;
    case PPE_HOOK_ADC_FAIL: g_adc_fail = true;  Serial.println("OK adc fail"); break;
    case PPE_HOOK_ADC_OK:   g_adc_fail = false; Serial.println("OK adc ok"); break;
    case PPE_HOOK_ADC_REAL: g_adc_inject = -1;  Serial.println("OK adc real"); break;
    case PPE_HOOK_MARKER_ON:  g_marker_on = true;  Serial.println("OK marker on"); break;
    case PPE_HOOK_MARKER_OFF: g_marker_on = false; Serial.println("OK marker off"); break;
    case PPE_HOOK_SET_SETTLE: ppe_cfg_set_settle(&cfg, h.arg); print_cfg(); Serial.println("OK set settle"); break;
    case PPE_HOOK_SET_GRACE:  ppe_cfg_set_grace(&cfg, h.arg);  print_cfg(); Serial.println("OK set grace"); break;
    case PPE_HOOK_SET_HB:     ppe_cfg_set_heartbeat(&cfg, h.arg); print_cfg(); Serial.println("OK set hb"); break;
    case PPE_HOOK_SHOW:
      print_cfg();
      Serial.printf("STATE confirmed=%d/%d candidate=%d/%d grace=%d adc_inject=%ld adc_fail=%d\n",
                    (int)confirm.confirmed.state, (int)confirm.confirmed.fault,
                    (int)settle.candidate.state, (int)settle.candidate.fault,
                    (int)confirm.in_grace, g_adc_inject, (int)g_adc_fail);
      print_node();
      break;
    case PPE_HOOK_TX_HB:
      if (g_run != RUN_ACTIVE) { Serial.println("ERR node is not active"); break; }
      Serial.println(ppe_tx_request_heartbeat(&txs) ? "OK tx hb" : "ERR heartbeat already requested");
      break;
    case PPE_HOOK_TX_EVENT: {
      if (g_run != RUN_ACTIVE) { Serial.println("ERR node is not active"); break; }
      ppe_event_t ev; ev.pair = ppe_raw_make(h.arg ? PPE_WEAR_WORN : PPE_WEAR_NOT_WORN, 0); ev.boot = false;
      Serial.println(ppe_tx_request_event(&txs, ev) ? "OK tx event" : "ERR event queue full");
      break;
    }
    case PPE_HOOK_SETCOUNTER: {       // the NEXT packet carries h.uarg + 1 (counter = last used value). The block is persisted first.
      if (g_run != RUN_ACTIVE) { Serial.println("ERR node is not active"); break; }
      uint64_t c = (uint64_t)h.uarg + PPE_COUNTER_BLOCK;
      uint32_t ceil_v = c > PPE_COUNTER_LIMIT ? PPE_COUNTER_LIMIT : (uint32_t)c;
      if (h.uarg > PPE_COUNTER_LIMIT) { Serial.println("ERR counter above the limit 4294967280"); break; }
      if (!store.write_ceiling(ceil_v)) { Serial.println("ERR NVS write failed"); break; }
      counter.resume(h.uarg, ceil_v);
      Serial.printf("OK setcounter last_used=%lu ceiling=%lu\n", (unsigned long)h.uarg, (unsigned long)ceil_v);
      break;
    }
    case PPE_HOOK_SKIPTX:     g_skip_n = (uint8_t)h.arg; Serial.printf("OK skiptx %ld\n", h.arg); break;
    case PPE_HOOK_NVSLOG_ON:  g_nvslog = true;  Serial.println("OK nvslog on"); break;
    case PPE_HOOK_NVSLOG_OFF: g_nvslog = false; Serial.println("OK nvslog off"); break;
    case PPE_HOOK_FAULT_NVS_WRITE: g_fail_nvs_write = true; Serial.println("OK fault nvs-write (next block write fails once)"); break;
    case PPE_HOOK_FAULT_BLE:  g_fail_ble_starts = 2; Serial.println("OK fault ble (the next two radio starts fail)"); break;
    case PPE_HOOK_FAULT_CLEAR: g_fail_nvs_write = false; g_fail_ble_starts = 0; Serial.println("OK fault clear (injected faults only; a NODE_FAULT clears on reset)"); break;
    case PPE_HOOK_AWAKE:      Serial.printf("OK awake %ld (this build does not sleep yet)\n", h.arg); break;
    case PPE_HOOK_BATT_SET:
    case PPE_HOOK_BATT_FAIL:
    case PPE_HOOK_BATT_REAL:  Serial.println("ERR batt hook: battery report comes with TC-13"); break;
    case PPE_HOOK_IDENT_SET:
      prefs.putUChar("node", (uint8_t)h.arg); prefs.putUChar("vest", (uint8_t)h.arg2);
      Serial.printf("OK ident set node=%ld vest=%ld, restarting\n", h.arg, h.arg2); Serial.flush(); delay(300); ESP.restart();
      break;
    case PPE_HOOK_IDENT_TESTKEY:
      prefs.putBytes("key", PPE_TEST_KEY, PPE_KEY_LEN);      // the public Appendix A test key, bench builds only
      Serial.println("OK ident testkey, restarting"); Serial.flush(); delay(300); ESP.restart();
      break;
    case PPE_HOOK_IDENT_CLEAR:
      prefs.remove("node"); prefs.remove("vest"); prefs.remove("key");   // the counter block stays: a counter is never reused
      Serial.println("OK ident clear, restarting"); Serial.flush(); delay(300); ESP.restart();
      break;
    case PPE_HOOK_IDENT_SHOW: print_node(); break;
    default: Serial.println("ERR bad command or argument"); break;
  }
}
static void poll_serial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') { g_line[g_len] = 0; g_len = 0; handle_line(g_line); }
    else if (g_len < sizeof(g_line) - 1) g_line[g_len++] = c;
  }
}
#else
static void poll_serial() {}
#endif

// COLD_BOOT (N-04): the counter block is advanced and persisted BEFORE any transmit. A failed write = NODE_FAULT (N-30).
static void begin_run() {
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_FSR, ADC_11db);
  pinMode(PIN_CONTACT_A, INPUT_PULLUP);
  pinMode(PIN_CONTACT_B, INPUT_PULLUP);
#ifdef PPE_TEST_HOOKS
  pinMode(PIN_MARKER, OUTPUT); digitalWrite(PIN_MARKER, LOW);
#endif
  ppe_cfg_defaults(&cfg);
  ppe_fsr_init(&fsr);                                     // COLD_START: no hysteresis history (R-5)
  ppe_tx_init(&txs);
  radio_init();                                           // once, before any data is set (radio rule)
  ppe_ctr_status_t st = counter.cold_boot();
  if (st != PPE_CTR_OK) { enter_fault(FAULT_NVS); return; }
  g_run = RUN_ACTIVE;
  uint32_t now = millis();
  last_raw = sample_raw();
  ppe_settle_init(&settle, last_raw);                     // N-04: candidate = first raw, no settle
  ppe_confirm_cold_start(&confirm, now);                  // N-20: boot grace starts
  next_poll = now + POLL_MS;
  PPE_LOGI("node", "COLD_START type=%d node=%u vest=%u", (int)PPE_BUILD_NODE_TYPE, (unsigned)ident.node_id, (unsigned)ident.vest_id);
#ifdef PPE_TEST_HOOKS
  print_cfg();
  mark_raw("COLD_START", last_raw);
  mark("GRACE_START");
#endif
}

void setup() {
  Serial.begin(115200);
  delay(500);
  if (!prefs.begin(NVS_NS, false)) { Serial.println("FAULT nvs open failed"); g_run = RUN_FAULT; g_fault = FAULT_NVS; return; }
  load_ident();
  if (!ppe_ident_valid(&ident)) {                         // N-01: UNPROVISIONED, no radio, no packet
    g_run = RUN_UNPROVISIONED;
    Serial.println("UNPROVISIONED: no valid identity in NVS, the radio is not started");
    return;
  }
  begin_run();
}

void loop() {
  poll_serial();
  if (g_run == RUN_ACTIVE) {
    uint32_t now = millis();
    tx_pump(now);                                          // runs every pass, not only on the 50 ms poll
    if ((int32_t)(now - next_poll) >= 0 && g_run == RUN_ACTIVE) {
      next_poll += POLL_MS;
      if ((int32_t)(now - next_poll) >= 0) next_poll = now + POLL_MS;   // fell behind: do not burst

      ppe_raw_t raw = sample_raw();
      if (!ppe_raw_eq(raw, last_raw)) { mark_raw("RAW", raw); last_raw = raw; }

      if (ppe_settle_step(&settle, now, raw, cfg.settle_ms)) {
        bool was = confirm.in_grace; uint32_t was_start = confirm.grace_start;
        mark_raw("CAND", settle.candidate); pulse();
        PPE_LOGI("node", "candidate state=%d fault=%d", (int)settle.candidate.state, (int)settle.candidate.fault);
        ppe_confirm_on_candidate(&confirm, now, settle.candidate);
        if (confirm.in_grace && (!was || confirm.grace_start != was_start)) mark("GRACE_START");
        else if (was && !confirm.in_grace) mark("GRACE_CANCEL");
      }

      ppe_event_t ev;
      if (ppe_confirm_tick(&confirm, now, settle.candidate, cfg.grace_s, &ev)) {
        mark_raw(ev.boot ? "CONFIRM_BOOT" : "CONFIRM", ev.pair); pulse();
        PPE_LOGI("node", "EVENT state=%d fault=%d boot=%d", (int)ev.pair.state, (int)ev.pair.fault, (int)ev.boot);
        if (!ppe_tx_request_event(&txs, ev)) PPE_LOGE("node", "event dropped: transmit queue full");
      }
    }
  }
  delay(1);
}
