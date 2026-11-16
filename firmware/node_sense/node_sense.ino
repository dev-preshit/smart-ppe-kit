// node_sense.ino - node firmware. S2: sensing and confirm logic. S4: radio, identity from NVS, counter block in NVS,
// event and heartbeat sequences (5 copies, 500 ms), deep sleep and wake (increment 7). No battery ADC yet (battery = 0xFF).
// Board: ESP32C3 Dev Module, USB CDC On Boot: Enabled. Library: ppe_common (linked from common/).
// Build flags (all optional):  -DPPE_TEST_HOOKS  -DPPE_BUILD_NODE_TYPE=0|1|2 (0 FOOT, 1 GLOVE, 2 HELMET)
//   -DPPE_CONTACT_CLOSED_LEVEL=0|1 (pin level when a contact is closed, default 0 = closed pulls to GND, B-02)
// A node with no valid identity in NVS (node_id 1..5, vest_id 1..254, key) is UNPROVISIONED: the radio is never started (N-01)
// and it never sleeps, so it can be provisioned over serial.
// Sleep (FR-14, FR-15): the node is in deep sleep except during boot, grace, transmission and a short settle. A wake is not a boot:
// the counter, ceiling, confirmed state, heartbeat deadline and sensor_fault come back from RTC memory with no NVS write (N-03).
// All times use one persistent clock (gettimeofday, kept by the RTC timer across deep sleep), in milliseconds, wrap-safe.
#include <Arduino.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <sys/time.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <driver/gpio.h>
#include <ppe_node.h>        // first: it brings ppe_proto.h, whose constants the other ppe headers then reuse
#include <ppe_sense.h>
#include <ppe_settle.h>
#include <ppe_confirm.h>
#include <ppe_config.h>
#include <ppe_log.h>
#include <ppe_counter.h>
#include <ppe_tx.h>
#include <ppe_rtcctx.h>
#include <ppe_sleep.h>
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
#define PPE_WAKE_LATENCY_MS 400u       // AS-17: boot after a GPIO wake takes about this long; it is taken off the grace time
#define PPE_FAULT_AWAKE_MS 60000u      // V12-06: a faulted node stays awake 60 s for serial, then sleeps with no wake source
#define PPE_FLUSH_HOLD_MS 2000u        // test builds: marks and the wake log are printed only after a wake that lasted this long anyway (USB is back by then)

// Pin map (B-01). GPIO4 is reserved for the S4 FSR wake comparator and is not used here (A-18: no comparator).
static const uint8_t PIN_FSR = 0;        // ADC1_CH0
static const uint8_t PIN_CONTACT_A = 1;  // glove strap contact, or helmet crown switch (RTC capable: can wake the chip)
static const uint8_t PIN_CONTACT_B = 3;  // helmet chin-strap contact (RTC capable)
static const uint8_t PIN_MARKER = 10;    // optional GPIO marker pulse (test builds)
static const uint32_t POLL_MS = 50;      // AS-03

static const char* const NVS_NS = "ppenode";
enum { RUN_UNPROVISIONED = 0, RUN_ACTIVE, RUN_FAULT };
enum { FAULT_NONE = 0, FAULT_LIMIT, FAULT_NVS, FAULT_BLE, FAULT_CRYPTO };
static const char* const FAULT_NAME[] = {"none", "counter-limit", "nvs-block-write", "ble-start", "crypto"};
enum { WAKE_BOOT = 0, WAKE_GPIO, WAKE_TIMER, WAKE_OTHER };
static const char* const WAKE_NAME[] = {"boot", "gpio", "timer", "other"};
static const char* const SLEEP_NAME[] = {"abort", "gpio", "poll", "forever"};

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
static uint32_t      g_boot_ms = 0;        // persistent clock at the very start of setup(): the boot grace is counted from here (TC-08)
static uint32_t      g_first_tx_us = 0;    // persistent clock (us) at the first copy start since this wake; 0 = none yet
static uint32_t      g_fault_ms = 0;

// ---- sleep state
RTC_DATA_ATTR static ppe_rtcctx_t g_rtc;   // the packed context (D-16)
RTC_DATA_ATTR static uint8_t g_rtc_valid = 0;
static ppe_sleep_state_t g_sleep;
static uint8_t  g_wake_kind = WAKE_BOOT;   // why this run started
static bool     g_resumed = false;
static bool     g_wake_noted = false;      // N-06 accounting is done once per wake
static bool     g_candidate_changed = false;
static bool     g_polled = false;          // at least one sensor poll since the start (never sleep before it)
static uint32_t g_backdate_ms = 0;         // AS-17: taken off the grace of the first candidate change after a GPIO wake
static uint32_t g_awake_until = 0;         // hook: awake <s>

// The persistent clock. After a deep sleep it keeps counting (RTC timer), so deadlines and timestamps survive it.
static uint64_t pnow_us() { struct timeval tv; gettimeofday(&tv, nullptr); return (uint64_t)tv.tv_sec * 1000000ull + (uint64_t)tv.tv_usec; }
static uint32_t pnow_ms() { return (uint32_t)(pnow_us() / 1000ull); }
static uint32_t pnow_us32() { return (uint32_t)pnow_us(); }

#ifdef PPE_TEST_HOOKS
#ifndef PPE_TEST_ADC_BOOT
#define PPE_TEST_ADC_BOOT -1          // -DPPE_TEST_ADC_BOOT=800 gives a power-on reading of 800 (TC-03 step 2)
#endif
// Test state that must survive deep sleep (RTC memory). Reset on every cold boot.
struct AtEntry { uint32_t due_ms; int32_t value; uint8_t used; };
struct MarkRec { char name[12]; uint32_t us; uint8_t has_raw, state, fault; };
struct WakeRec { uint32_t seq, t_wake_ms, awake_ms, hb_rem_ms; uint8_t cause, noop, poll, mode; };
#define MARK_RING 24
#define WAKE_RING 12
RTC_DATA_ATTR static int32_t  g_adc_inject = PPE_TEST_ADC_BOOT;   // -1 = use the real ADC
RTC_DATA_ATTR static uint8_t  g_adc_fail = 0;
RTC_DATA_ATTR static uint8_t  g_marker_on = 1;
RTC_DATA_ATTR static uint8_t  g_nvslog = 0;
RTC_DATA_ATTR static uint8_t  g_skip_n = 0;
RTC_DATA_ATTR static uint8_t  g_sleep_on = 1;
RTC_DATA_ATTR static uint8_t  g_cfg_valid = 0;
RTC_DATA_ATTR static ppe_cfg_t g_cfg_rtc;
RTC_DATA_ATTR static AtEntry  g_at[4];
RTC_DATA_ATTR static MarkRec  g_marks[MARK_RING];
RTC_DATA_ATTR static uint8_t  g_marks_n = 0;
RTC_DATA_ATTR static uint8_t  g_marks_lost = 0;
RTC_DATA_ATTR static WakeRec  g_wlog[WAKE_RING];
RTC_DATA_ATTR static uint8_t  g_wlog_n = 0;
RTC_DATA_ATTR static uint32_t g_wake_total = 0;
static bool g_fail_nvs_write = false;
static uint8_t g_fail_ble_starts = 0;
static bool g_seq_suppressed = false;
static char g_line[48];
static uint8_t g_len = 0;

static void test_cold_init() {         // a cold boot starts every hook from its default
  g_adc_inject = PPE_TEST_ADC_BOOT; g_adc_fail = 0; g_marker_on = 1; g_nvslog = 0; g_skip_n = 0; g_sleep_on = 1; g_cfg_valid = 0;
  memset(g_at, 0, sizeof g_at); g_marks_n = 0; g_marks_lost = 0; g_wlog_n = 0; g_wake_total = 0;
}
static void mark_add(const char* name, uint32_t us, bool has_raw, ppe_raw_t r) {
  if (g_marks_n >= MARK_RING) { if (g_marks_lost < 255) g_marks_lost++; return; }
  MarkRec& m = g_marks[g_marks_n++];
  strncpy(m.name, name, sizeof m.name - 1); m.name[sizeof m.name - 1] = 0;
  m.us = us; m.has_raw = has_raw; m.state = (uint8_t)r.state; m.fault = r.fault;
}
// Marks are buffered in RTC memory and printed later, once the USB port is back (it disappears in deep sleep).
static void mark(const char* name) { mark_add(name, pnow_us32(), false, ppe_raw_make(PPE_WEAR_NONE, 0)); }
static void mark_us(const char* name, uint32_t us) { mark_add(name, us, false, ppe_raw_make(PPE_WEAR_NONE, 0)); }
static void mark_raw(const char* name, ppe_raw_t r) { mark_add(name, pnow_us32(), true, r); }
static void pulse() { if (g_marker_on) { digitalWrite(PIN_MARKER, HIGH); delayMicroseconds(100); digitalWrite(PIN_MARKER, LOW); } }
static void print_cfg() { char b[96]; ppe_cfg_fmt(b, sizeof b, (ppe_node_type_t)PPE_BUILD_NODE_TYPE, &cfg); Serial.println(b); }
static void flush_marks() {
  for (uint8_t i = 0; i < g_marks_n; i++) {
    char b[64];
    if (g_marks[i].has_raw) ppe_mark_fmt_raw(b, sizeof b, g_marks[i].name, g_marks[i].us, ppe_raw_make((ppe_wear_t)g_marks[i].state, g_marks[i].fault));
    else ppe_mark_fmt(b, sizeof b, g_marks[i].name, g_marks[i].us);
    Serial.println(b);
  }
  if (g_marks_lost) { Serial.printf("MARKS LOST %u (ring full)\n", (unsigned)g_marks_lost); g_marks_lost = 0; }
  g_marks_n = 0;
}
static void flush_wakelog() {
  for (uint8_t i = 0; i < g_wlog_n; i++) {
    const WakeRec& w = g_wlog[i];
    Serial.printf("WAKE seq=%lu t_wake_ms=%lu awake_ms=%lu cause=%s noop=%u poll=%u sleep=%s hb_rem_ms=%lu\n", (unsigned long)w.seq,
                  (unsigned long)w.t_wake_ms, (unsigned long)w.awake_ms, WAKE_NAME[w.cause], (unsigned)w.noop, (unsigned)w.poll,
                  SLEEP_NAME[w.mode], (unsigned long)w.hb_rem_ms);
  }
  g_wlog_n = 0;
}
static void wake_record(uint32_t now, uint8_t mode, uint32_t hb_rem) {
  // A wake that only sampled the ADC and went back to sleep is counted but not logged: a FOOT node does that every second.
  bool interesting = g_wake_kind != WAKE_TIMER || (now - g_boot_ms) > 1000u || g_sleep.gpio_noop_wakes || g_sleep.poll_mode;
  if (!interesting) return;
  if (g_wlog_n >= WAKE_RING) { for (uint8_t i = 1; i < WAKE_RING; i++) g_wlog[i - 1] = g_wlog[i]; g_wlog_n = WAKE_RING - 1; }
  WakeRec& w = g_wlog[g_wlog_n++];
  w.seq = g_wake_total; w.t_wake_ms = g_boot_ms; w.awake_ms = now - g_boot_ms; w.hb_rem_ms = hb_rem;
  w.cause = g_wake_kind; w.noop = g_sleep.gpio_noop_wakes; w.poll = g_sleep.poll_mode; w.mode = mode;
}
static void apply_adcat(uint32_t now) {
  for (uint8_t i = 0; i < 4; i++)
    if (g_at[i].used && (int32_t)(now - g_at[i].due_ms) >= 0) { g_adc_inject = g_at[i].value; g_at[i].used = 0; mark("ADCAT"); }   // the mark is the exact time the injected reading changed
}
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
  g_fault_ms = pnow_ms();
  ppe_tx_abort(&txs);
  if (adv) adv->stop();
  Serial.printf("FAULT %s: the node stops transmitting until a reset\n", FAULT_NAME[why]);
}

// ---- radio. The radio rule (S3 finding): connectable NON, discoverable NON, no scan response and the intervals are set ONCE here,
// before any data is set. Later only setAdvertisementData() and start()/stop() are called, otherwise NimBLE sends ADV_SCAN_IND
// or appends 02 01 04 (24 bytes) and the hub drops the packet as DROP_LEN. The radio is started on the first transmit of a wake.
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
  if (!g_radio_up) radio_init();
  NimBLEAdvertisementData ad;
  ad.addData(g_adv, PPE_ADV_LEN);                             // raw bytes, nothing added by the library
  adv->setAdvertisementData(ad);
  return adv->start(PPE_ADV_BURST_MS_DEFAULT);
}

// T-07: retry the start once, a second failure is NODE_FAULT (N-31). The mark is taken just before the start (A-20).
static bool start_copy(uint8_t k) {
  uint32_t us = pnow_us32();
  bool ok = start_copy_once();
  if (!ok) { delay(5); us = pnow_us32(); ok = start_copy_once(); }
  if (!ok) return false;
  char name[8]; snprintf(name, sizeof name, "COPY%u", (unsigned)k);
  mark_us(name, us);
  if (k == 0) { pulse(); if (!g_first_tx_us) g_first_tx_us = us ? us : 1u; }   // marker hook: the first copy start
  return true;
}

static void stop_copy() {
#ifdef PPE_TEST_HOOKS
  if (g_seq_suppressed) return;
#endif
  if (adv) adv->stop();
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

// ---- deep sleep (N-06, N-07, N-10)
static void arm_gpio_wake(const ppe_sleep_out_t& o) {
  uint64_t mask = 0;
  if (o.pin_mask & 1u) mask |= 1ull << PIN_CONTACT_A;
  if (o.pin_mask & 2u) mask |= 1ull << PIN_CONTACT_B;
  int level = o.wake_on_closed ? PPE_CONTACT_CLOSED_LEVEL : !PPE_CONTACT_CLOSED_LEVEL;
  for (uint8_t pin = 0; pin < 8; pin++) {
    if (!(mask & (1ull << pin))) continue;
    gpio_pullup_en((gpio_num_t)pin);                        // a contact is open or pulled to ground: the pull-up must outlive the sleep
    gpio_pulldown_dis((gpio_num_t)pin);
    gpio_hold_en((gpio_num_t)pin);                          // the C3 has no RTC output pads: hold the digital pad configuration instead
  }
  gpio_deep_sleep_hold_en();
  esp_deep_sleep_enable_gpio_wakeup(mask, level ? ESP_GPIO_WAKEUP_GPIO_HIGH : ESP_GPIO_WAKEUP_GPIO_LOW);
}

static bool sleep_allowed(uint32_t now) {
#ifdef PPE_TEST_HOOKS
  if (!g_sleep_on) return false;
#endif
  return (int32_t)(now - g_awake_until) >= 0;
}

// N-06: filter idle AND confirm stable AND transmit idle AND no request pending.
static bool idle_for_sleep(uint32_t now) {
  return g_polled && sleep_allowed(now) && !confirm.in_grace && !settle.pending && !ppe_tx_busy(&txs) && txs.ev_n == 0 && !txs.hb_req &&
         !ppe_hb_due(&confirm, now) && confirm.confirmed.state != PPE_WEAR_NONE;
}

static void go_to_sleep(uint32_t now) {
  bool faulted = (g_run == RUN_FAULT);
  if (!faulted && !g_wake_noted) { ppe_sleep_note_wake(&g_sleep, g_wake_kind == WAKE_GPIO, g_candidate_changed); g_wake_noted = true; }
  ppe_sleep_in_t in;
  in.type = (ppe_node_type_t)PPE_BUILD_NODE_TYPE;
  in.confirmed = confirm.confirmed.state;
  in.contact_a_closed = contact_closed(PIN_CONTACT_A);
  in.contact_b_closed = contact_closed(PIN_CONTACT_B);
  in.hb_set = confirm.hb_set;
  in.hb_remaining_ms = confirm.hb_set ? ((int32_t)(confirm.hb_deadline - now) > 0 ? confirm.hb_deadline - now : 0u) : 0u;
  in.node_fault = faulted;
  ppe_sleep_out_t out;
  ppe_sleep_decide(&g_sleep, &in, &out);
  if (out.mode == PPE_SLEEP_ABORT) { ppe_sleep_note_abort(&g_sleep); return; }   // N-07b: stay awake, decide again on the next pass
#ifdef PPE_TEST_HOOKS
  wake_record(now, (uint8_t)out.mode, in.hb_remaining_ms);
  if ((int32_t)(pnow_ms() - g_boot_ms) >= (int32_t)PPE_FLUSH_HOLD_MS) { flush_marks(); flush_wakelog(); Serial.flush(); delay(250); }   // short wakes stay buffered in RTC memory; the wait lets the USB host read the tail before the chip sleeps
#endif
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  if (out.mode == PPE_SLEEP_FOREVER) {
    g_rtc_valid = 0;                                          // only a reset wakes it, and a reset is a cold boot
  } else {
    ppe_rtc_fields_t f;
    f.counter = counter.counter(); f.ceiling = counter.ceiling(); f.hb_deadline = confirm.hb_deadline;
    f.confirmed_state = confirm.confirmed.state; f.sensor_fault = confirm.confirmed.fault; f.boot_pending = confirm.boot_pending;
    f.gpio_noop_wakes = g_sleep.gpio_noop_wakes; f.poll_mode = g_sleep.poll_mode; f.hb_set = confirm.hb_set;
    ppe_rtcctx_pack(&g_rtc, &f);
    g_rtc_valid = 1;
    if (out.timer_valid) esp_sleep_enable_timer_wakeup((uint64_t)(out.timer_ms ? out.timer_ms : 1u) * 1000ull);
    if (out.mode == PPE_SLEEP_GPIO) arm_gpio_wake(out);
  }
  esp_deep_sleep_start();
}

#ifdef PPE_TEST_HOOKS
static void print_node() {
  Serial.printf("NODE run=%s prov=%d node=%u vest=%u key=%s fault=%s radio=%d ctr=%lu ceil=%lu tx_state=%d skip=%u boot_us=%lu first_tx_us=%lu\n",
                g_run == RUN_ACTIVE ? "active" : (g_run == RUN_FAULT ? "fault" : "unprovisioned"), (int)ppe_ident_valid(&ident),
                (unsigned)ident.node_id, (unsigned)ident.vest_id, ident.has_key ? "yes" : "no", FAULT_NAME[g_fault], (int)g_radio_up,
                (unsigned long)counter.counter(), (unsigned long)counter.ceiling(), (int)txs.state, (unsigned)g_skip_n,
                (unsigned long)(g_boot_ms * 1000u), (unsigned long)g_first_tx_us);
  Serial.printf("SLEEP on=%u wake=%s resumed=%d noop=%u poll=%u wakes=%lu awake_until_ms=%lu now_ms=%lu\n", (unsigned)g_sleep_on,
                WAKE_NAME[g_wake_kind], (int)g_resumed, (unsigned)g_sleep.gpio_noop_wakes, (unsigned)g_sleep.poll_mode,
                (unsigned long)g_wake_total, (unsigned long)g_awake_until, (unsigned long)pnow_ms());
}

static void handle_line(const char* line) {
  ppe_hook_cmd_t h;
  switch (ppe_hook_parse(line, &h)) {
    case PPE_HOOK_EMPTY: break;
    case PPE_HOOK_ADC_SET:  g_adc_inject = (int32_t)h.arg; Serial.printf("OK adc %ld\n", h.arg); break;
    case PPE_HOOK_ADC_FAIL: g_adc_fail = 1;  Serial.println("OK adc fail"); break;
    case PPE_HOOK_ADC_OK:   g_adc_fail = 0; Serial.println("OK adc ok"); break;
    case PPE_HOOK_ADC_REAL: g_adc_inject = -1;  Serial.println("OK adc real"); break;
    case PPE_HOOK_MARKER_ON:  g_marker_on = 1;  Serial.println("OK marker on"); break;
    case PPE_HOOK_MARKER_OFF: g_marker_on = 0; Serial.println("OK marker off"); break;
    case PPE_HOOK_SET_SETTLE: ppe_cfg_set_settle(&cfg, h.arg); g_cfg_rtc = cfg; g_cfg_valid = 1; print_cfg(); Serial.println("OK set settle"); break;
    case PPE_HOOK_SET_GRACE:  ppe_cfg_set_grace(&cfg, h.arg);  g_cfg_rtc = cfg; g_cfg_valid = 1; print_cfg(); Serial.println("OK set grace"); break;
    case PPE_HOOK_SET_HB:     ppe_cfg_set_heartbeat(&cfg, h.arg); g_cfg_rtc = cfg; g_cfg_valid = 1; print_cfg(); Serial.println("OK set hb"); break;
    case PPE_HOOK_SHOW:
      print_cfg();
      Serial.printf("STATE confirmed=%d/%d candidate=%d/%d grace=%d adc_inject=%ld adc_fail=%d\n",
                    (int)confirm.confirmed.state, (int)confirm.confirmed.fault,
                    (int)settle.candidate.state, (int)settle.candidate.fault,
                    (int)confirm.in_grace, (long)g_adc_inject, (int)g_adc_fail);
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
    case PPE_HOOK_NVSLOG_ON:  g_nvslog = 1;  Serial.println("OK nvslog on"); break;
    case PPE_HOOK_NVSLOG_OFF: g_nvslog = 0; Serial.println("OK nvslog off"); break;
    case PPE_HOOK_FAULT_NVS_WRITE: g_fail_nvs_write = true; Serial.println("OK fault nvs-write (next block write fails once)"); break;
    case PPE_HOOK_FAULT_BLE:  g_fail_ble_starts = 2; Serial.println("OK fault ble (the next two radio starts fail)"); break;
    case PPE_HOOK_FAULT_CLEAR: g_fail_nvs_write = false; g_fail_ble_starts = 0; Serial.println("OK fault clear (injected faults only; a NODE_FAULT clears on reset)"); break;
    case PPE_HOOK_AWAKE:      g_awake_until = pnow_ms() + (uint32_t)h.arg * 1000u; Serial.printf("OK awake %ld s\n", h.arg); break;
    case PPE_HOOK_SLEEP_ON:   g_sleep_on = 1; Serial.println("OK sleep on"); break;
    case PPE_HOOK_SLEEP_OFF:  g_sleep_on = 0; Serial.println("OK sleep off (until the next cold boot)"); break;
    case PPE_HOOK_ADCAT: {            // the injected reading becomes h.arg2 after h.arg seconds, even through deep sleep
      uint8_t slot = 4;
      for (uint8_t i = 0; i < 4; i++) if (!g_at[i].used) { slot = i; break; }
      if (slot == 4) { Serial.println("ERR adcat: 4 entries are already waiting (adcat clear)"); break; }
      g_at[slot].due_ms = pnow_ms() + (uint32_t)h.arg * 1000u; g_at[slot].value = (int32_t)h.arg2; g_at[slot].used = 1;
      Serial.printf("OK adcat in %ld s -> %ld (now_ms=%lu due_ms=%lu)\n", h.arg, h.arg2, (unsigned long)pnow_ms(), (unsigned long)g_at[slot].due_ms);
      break;
    }
    case PPE_HOOK_ADCAT_CLEAR: memset(g_at, 0, sizeof g_at); Serial.println("OK adcat clear"); break;
    case PPE_HOOK_WAKELOG:    flush_marks(); flush_wakelog(); Serial.println("OK wakelog"); break;
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

static void init_io() {
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_FSR, ADC_11db);
  gpio_deep_sleep_hold_dis();                               // release the pads held through deep sleep
  gpio_hold_dis((gpio_num_t)PIN_CONTACT_A);
  gpio_hold_dis((gpio_num_t)PIN_CONTACT_B);
  pinMode(PIN_CONTACT_A, INPUT_PULLUP);
  pinMode(PIN_CONTACT_B, INPUT_PULLUP);
#ifdef PPE_TEST_HOOKS
  pinMode(PIN_MARKER, OUTPUT); digitalWrite(PIN_MARKER, LOW);
#endif
  ppe_tx_init(&txs);
}

// COLD_BOOT (N-04): the counter block is advanced and persisted BEFORE any transmit. A failed write = NODE_FAULT (N-30).
static void begin_run() {
  init_io();
  ppe_cfg_defaults(&cfg);
#ifdef PPE_TEST_HOOKS
  test_cold_init();
#endif
  ppe_fsr_init(&fsr);                                     // COLD_START: no hysteresis history (R-5)
  ppe_sleep_state_init(&g_sleep);
  ppe_ctr_status_t st = counter.cold_boot();
  if (st != PPE_CTR_OK) { enter_fault(FAULT_NVS); return; }
  g_run = RUN_ACTIVE;
  uint32_t now = pnow_ms();
  last_raw = sample_raw();
  ppe_settle_init(&settle, last_raw);                     // N-04: candidate = first raw, no settle
  ppe_confirm_cold_start(&confirm, g_boot_ms);            // N-20: the 25 s boot grace counts from the start of setup(), not from after the init
  next_poll = now + POLL_MS;
  PPE_LOGI("node", "COLD_START type=%d node=%u vest=%u", (int)PPE_BUILD_NODE_TYPE, (unsigned)ident.node_id, (unsigned)ident.vest_id);
#ifdef PPE_TEST_HOOKS
  print_cfg();
  mark_raw("COLD_START", last_raw);
  mark_us("GRACE_START", g_boot_ms * 1000u);
#endif
}

// RESUME (N-03): a deep-sleep wake with a valid RTC context. No NVS write, no BOOT=1.
static void resume_run(const ppe_rtc_fields_t& f) {
  init_io();
  ppe_cfg_defaults(&cfg);
#ifdef PPE_TEST_HOOKS
  if (g_cfg_valid) cfg = g_cfg_rtc;
#endif
  g_resumed = true;
#ifdef PPE_TEST_HOOKS
  g_wake_total++;                                         // every wake is counted, only the interesting ones are logged
#endif
  ppe_confirm_resume(&confirm, ppe_raw_make(f.confirmed_state, f.sensor_fault), f.boot_pending != 0, f.hb_set != 0, f.hb_deadline);
  counter.resume(f.counter, f.ceiling);
  ppe_fsr_seed(&fsr, f.confirmed_state);                  // A-08: the hysteresis memory is the confirmed state
  g_sleep.gpio_noop_wakes = f.gpio_noop_wakes;
  g_sleep.poll_mode = f.poll_mode != 0;
  g_run = RUN_ACTIVE;
  uint32_t now = pnow_ms();
#ifdef PPE_TEST_HOOKS
  apply_adcat(now);
#endif
  last_raw = sample_raw();
  ppe_settle_init(&settle, confirm.confirmed);            // resume: candidate = confirmed, so a different reading is a candidate change at once
  next_poll = now;                                        // poll at once
  g_backdate_ms = (g_wake_kind == WAKE_GPIO) ? PPE_WAKE_LATENCY_MS : 0u;
}

void setup() {
  g_boot_ms = pnow_ms();                                  // first thing: the TC-08 clock (the ROM and bootloader time before this is not counted)
  Serial.begin(115200);
  esp_reset_reason_t reason = esp_reset_reason();
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  g_wake_kind = (reason != ESP_RST_DEEPSLEEP) ? WAKE_BOOT : (cause == ESP_SLEEP_WAKEUP_GPIO ? WAKE_GPIO : (cause == ESP_SLEEP_WAKEUP_TIMER ? WAKE_TIMER : WAKE_OTHER));
  if (!prefs.begin(NVS_NS, false)) { Serial.println("FAULT nvs open failed"); g_run = RUN_FAULT; g_fault = FAULT_NVS; g_fault_ms = g_boot_ms; return; }
  load_ident();
  if (!ppe_ident_valid(&ident)) {                         // N-01: UNPROVISIONED, no radio, no packet, no sleep
    g_run = RUN_UNPROVISIONED;
    g_rtc_valid = 0;
    Serial.println("UNPROVISIONED: no valid identity in NVS, the radio is not started");
    return;
  }
  ppe_rtc_fields_t f;
  ppe_rtc_result_t rr = ppe_rtcctx_unpack(&g_rtc, reason == ESP_RST_DEEPSLEEP, g_rtc_valid != 0, &f);
  g_rtc_valid = 0;                                        // consumed: a stale image must never be used twice
  if (rr == PPE_RTC_OK) resume_run(f); else begin_run();
}

void loop() {
  poll_serial();
  uint32_t now = pnow_ms();
  if (g_run == RUN_ACTIVE) {
#ifdef PPE_TEST_HOOKS
    apply_adcat(now);
    if (g_marks_n && (int32_t)(now - g_boot_ms) >= (int32_t)(PPE_FLUSH_HOLD_MS - 500)) flush_marks();   // only once the port is likely back
#endif
    tx_pump(now);
    if (g_run == RUN_ACTIVE && (int32_t)(now - next_poll) >= 0) {
      next_poll += POLL_MS;
      if ((int32_t)(now - next_poll) >= 0) next_poll = now + POLL_MS;   // fell behind: do not burst

      ppe_raw_t raw = sample_raw();
      g_polled = true;
      if (!ppe_raw_eq(raw, last_raw)) { mark_raw("RAW", raw); last_raw = raw; }

      if (ppe_settle_step(&settle, now, raw, cfg.settle_ms)) {
        g_candidate_changed = true;
        bool was = confirm.in_grace; uint32_t was_start = confirm.grace_start;
        mark_raw("CAND", settle.candidate); pulse();
        PPE_LOGI("node", "candidate state=%d fault=%d", (int)settle.candidate.state, (int)settle.candidate.fault);
        ppe_confirm_on_candidate(&confirm, now - g_backdate_ms, settle.candidate);   // AS-17: the wake latency is taken off the grace
        g_backdate_ms = 0;
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
    if (g_run == RUN_ACTIVE && idle_for_sleep(now)) go_to_sleep(now);       // N-06, N-07
  } else if (g_run == RUN_FAULT) {
    if ((int32_t)(now - g_fault_ms) >= (int32_t)PPE_FAULT_AWAKE_MS) go_to_sleep(now);   // V12-06
  }
  delay(1);
}
