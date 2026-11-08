// node_sense.ino - S2: node sensing and confirm logic. No radio, no deep sleep, no NVS (identity = test constants).
// Board: ESP32C3 Dev Module, USB CDC On Boot: Enabled. Library: ppe_common (linked from common/).
// Build flags (all optional):  -DPPE_TEST_HOOKS  -DPPE_BUILD_NODE_TYPE=0|1|2 (0 FOOT, 1 GLOVE, 2 HELMET)
//   -DPPE_CONTACT_CLOSED_LEVEL=0|1 (pin level when a contact is closed, default 0 = closed pulls to GND, B-02)
#include <Arduino.h>
#include <ppe_sense.h>
#include <ppe_settle.h>
#include <ppe_confirm.h>
#include <ppe_config.h>
#include <ppe_log.h>
#ifdef PPE_TEST_HOOKS
#include <ppe_hooks.h>
#endif

// Numeric on purpose: enum names are not visible to the preprocessor.
#ifndef PPE_BUILD_NODE_TYPE
#define PPE_BUILD_NODE_TYPE 0
#endif
#ifndef PPE_CONTACT_CLOSED_LEVEL
#define PPE_CONTACT_CLOSED_LEVEL 0
#endif

// Pin map (B-01). GPIO4 is reserved for the S4 FSR wake comparator and is not used here.
static const uint8_t PIN_FSR = 0;        // ADC1_CH0
static const uint8_t PIN_CONTACT_A = 1;  // glove strap contact, or helmet crown switch
static const uint8_t PIN_CONTACT_B = 3;  // helmet chin-strap contact
static const uint8_t PIN_MARKER = 10;    // optional GPIO marker pulse (test builds)
static const uint32_t POLL_MS = 50;      // AS-03

static ppe_cfg_t     cfg;
static ppe_fsr_t     fsr;
static ppe_settle_t  settle;
static ppe_confirm_t confirm;
static ppe_raw_t     last_raw;
static uint32_t      next_poll;

#ifdef PPE_TEST_HOOKS
#ifndef PPE_TEST_ADC_BOOT
#define PPE_TEST_ADC_BOOT -1          // -DPPE_TEST_ADC_BOOT=800 gives a power-on reading of 800 (TC-03 step 2)
#endif
static long g_adc_inject = PPE_TEST_ADC_BOOT;   // -1 = use the real ADC
static bool g_adc_fail = false;
static bool g_marker_on = true;
static char g_line[48];
static uint8_t g_len = 0;

static void mark(const char* name) { char b[48]; ppe_mark_fmt(b, sizeof b, name, micros()); Serial.println(b); }
static void mark_raw(const char* name, ppe_raw_t r) { char b[64]; ppe_mark_fmt_raw(b, sizeof b, name, micros(), r); Serial.println(b); }
static void pulse() { if (g_marker_on) { digitalWrite(PIN_MARKER, HIGH); delayMicroseconds(100); digitalWrite(PIN_MARKER, LOW); } }
static void print_cfg() { char b[96]; ppe_cfg_fmt(b, sizeof b, (ppe_node_type_t)PPE_BUILD_NODE_TYPE, &cfg); Serial.println(b); }
#else
static void mark(const char*) {}
static void mark_raw(const char*, ppe_raw_t) {}
static void pulse() {}
#endif

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
    case PPE_HOOK_SHOW:
      print_cfg();
      Serial.printf("STATE confirmed=%d/%d candidate=%d/%d grace=%d adc_inject=%ld adc_fail=%d\n",
                    (int)confirm.confirmed.state, (int)confirm.confirmed.fault,
                    (int)settle.candidate.state, (int)settle.candidate.fault,
                    (int)confirm.in_grace, g_adc_inject, (int)g_adc_fail);
      break;
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

void setup() {
  Serial.begin(115200);
  delay(500);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_FSR, ADC_11db);
  pinMode(PIN_CONTACT_A, INPUT_PULLUP);
  pinMode(PIN_CONTACT_B, INPUT_PULLUP);
#ifdef PPE_TEST_HOOKS
  pinMode(PIN_MARKER, OUTPUT); digitalWrite(PIN_MARKER, LOW);
#endif
  ppe_cfg_defaults(&cfg);
  ppe_fsr_init(&fsr);                                     // COLD_START: no hysteresis history (R-5)
  uint32_t now = millis();
  last_raw = sample_raw();
  ppe_settle_init(&settle, last_raw);                     // N-04: candidate = first raw, no settle
  ppe_confirm_cold_start(&confirm, now);                  // N-20: boot grace starts
  next_poll = now + POLL_MS;
  PPE_LOGI("node", "COLD_START type=%d", (int)PPE_BUILD_NODE_TYPE);
#ifdef PPE_TEST_HOOKS
  print_cfg();
  mark_raw("COLD_START", last_raw);
  mark("GRACE_START");
#endif
}

void loop() {
  poll_serial();
  uint32_t now = millis();
  if ((int32_t)(now - next_poll) < 0) return;
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
  }
}
