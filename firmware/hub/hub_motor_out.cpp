#include "hub_motor_out.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "soc/gpio_reg.h"
#include "src/hub_config.h"

static HubMotor g_motor;
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool g_alert_req = false, g_key_req = false;
static volatile uint32_t g_trips_unreported = 0;
static esp_timer_handle_t g_guard_timer = nullptr;

static volatile bool g_force_low = false;      // set by the task watchdog ISR: the output stays low until the restart

static inline uint32_t now_us() { return (uint32_t)esp_timer_get_time(); }
static inline void drive() { digitalWrite(HUB_MOTOR_PIN, (g_motor.output() && !g_force_low) ? HIGH : LOW); }   // call with g_mux held

// M-08 / BUG-007: on a task watchdog timeout the panic handler prints for about half a second before the restart, and
// neither the motor task nor the guard timer runs meanwhile. ESP-IDF calls this weak hook from the watchdog ISR before
// the panic: drive the motor pin low by a direct register write (IRAM, no locks) and keep it low until the restart.
static_assert(HUB_MOTOR_PIN < 32, "GPIO_OUT_W1TC_REG covers GPIO 0..31 only");
extern "C" void IRAM_ATTR esp_task_wdt_isr_user_handler(void) {
  g_force_low = true;
  REG_WRITE(GPIO_OUT_W1TC_REG, 1UL << HUB_MOTOR_PIN);
}

void hub_motor_early_init() {
  pinMode(HUB_MOTOR_PIN, OUTPUT);
  digitalWrite(HUB_MOTOR_PIN, LOW);            // B-01: low as early as the firmware can; the external pull-down covers the time before
}

#ifdef PPE_TEST_HOOKS
static volatile bool g_fault_guard = false;
static bool g_stall_active = false;            // only the motor task touches these two
static uint32_t g_stall_until = 0;
void hub_motor_fault_guard() { g_fault_guard = true; }
#endif

static void motor_task(void*) {
  esp_task_wdt_add(NULL);                      // X-02: a stuck motor task resets the chip (the pin is low again after the reset, B-01)
  bool key_applied = false;
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&last, 1);                 // 1 tick = 1 ms (arduino-esp32: CONFIG_FREERTOS_HZ = 1000)
    const uint32_t t = now_us();
    const bool want = g_alert_req, key = g_key_req;
    portENTER_CRITICAL(&g_mux);
    if (key != key_applied) { key_applied = key; g_motor.set_key_fault(key, t); }   // M-09
    g_motor.set_alert(want, t);                // M-01 / M-06
#ifdef PPE_TEST_HOOKS
    if (g_fault_guard && g_motor.output()) { g_stall_active = true; g_stall_until = t + 1500000u; g_fault_guard = false; }
    if (g_stall_active && (int32_t)(t - g_stall_until) >= 0) g_stall_active = false;
    if (!g_stall_active) g_motor.pattern_step(t);   // M-02 / M-03 / M-04 (stalled on purpose by fault guard)
#else
    g_motor.pattern_step(t);                   // M-02 / M-03 / M-04
#endif
    drive();
    portEXIT_CRITICAL(&g_mux);
    esp_task_wdt_reset();
  }
}

static void guard_cb(void*) {                  // M-05: runs even when the motor task is stuck
  const uint32_t t = now_us();
  portENTER_CRITICAL(&g_mux);
  g_motor.guard_step(t);
  drive();
  const bool trip = g_motor.take_guard_trip();
  portEXIT_CRITICAL(&g_mux);
  if (trip) g_trips_unreported = g_trips_unreported + 1;
}

#ifdef PPE_TEST_HOOKS
static esp_timer_handle_t g_marker_timer = nullptr, g_pulse_timer = nullptr;
static volatile bool g_marker_on = false, g_pulse_level = false;
static volatile uint32_t g_pulse_on_us = 0, g_pulse_off_us = 0, g_pulse_left = 0;

static void marker_off_cb(void*) { digitalWrite(HUB_MARKER_PIN, LOW); }

static void pulse_cb(void*) {
  if (!g_pulse_level) {
    if (g_pulse_left == 0) return;
    digitalWrite(HUB_MARKER_PIN, HIGH);
    g_pulse_level = true;
    esp_timer_start_once(g_pulse_timer, g_pulse_on_us);
  } else {
    digitalWrite(HUB_MARKER_PIN, LOW);
    g_pulse_level = false;
    g_pulse_left = g_pulse_left - 1;
    if (g_pulse_left > 0) esp_timer_start_once(g_pulse_timer, g_pulse_off_us);
  }
}

static void test_pins_start() {
  pinMode(HUB_MARKER_PIN, OUTPUT);
  digitalWrite(HUB_MARKER_PIN, LOW);
  esp_timer_create_args_t a = {};
  a.dispatch_method = ESP_TIMER_TASK;
  a.name = "ppe_marker"; a.callback = marker_off_cb;
  esp_timer_create(&a, &g_marker_timer);
  a.name = "ppe_pulse"; a.callback = pulse_cb;
  esp_timer_create(&a, &g_pulse_timer);
}

void hub_marker_pulse() {
  if (!g_marker_on || !g_marker_timer) return;
  digitalWrite(HUB_MARKER_PIN, HIGH);
  esp_timer_stop(g_marker_timer);
  esp_timer_start_once(g_marker_timer, HUB_MARKER_PULSE_US);
}

void hub_hook_marker(bool on) {
  if (g_pulse_timer) esp_timer_stop(g_pulse_timer);
  g_pulse_left = 0; g_pulse_level = false;
  digitalWrite(HUB_MARKER_PIN, LOW);
  g_marker_on = on;
  Serial.printf("PPE_HOOK marker %s ok pin=%d t_ms=%lu\n", on ? "on" : "off", (int)HUB_MARKER_PIN, (unsigned long)(esp_timer_get_time() / 1000));
}

void hub_hook_pulse(uint32_t on_ms, uint32_t off_ms, uint32_t count) {
  if (!g_pulse_timer || on_ms == 0 || off_ms == 0 || on_ms > 5000 || off_ms > 5000 || count == 0 || count > 200) {
    Serial.println("PPE_HOOK pulse REFUSED(on and off 1..5000 ms, count 1..200)");
    return;
  }
  esp_timer_stop(g_pulse_timer);
  g_marker_on = false;
  digitalWrite(HUB_MARKER_PIN, LOW);
  g_pulse_on_us = on_ms * 1000u; g_pulse_off_us = off_ms * 1000u; g_pulse_left = count; g_pulse_level = false;
  esp_timer_start_once(g_pulse_timer, 500000);
  Serial.printf("PPE_HOOK pulse %lu %lu %lu ok pin=%d t_ms=%lu\n", (unsigned long)on_ms, (unsigned long)off_ms, (unsigned long)count,
                (int)HUB_MARKER_PIN, (unsigned long)(esp_timer_get_time() / 1000));
}
#endif

void hub_motor_start() {
  esp_timer_create_args_t a = {};
  a.callback = guard_cb;
  a.name = "ppe_guard";
  a.dispatch_method = ESP_TIMER_TASK;
  if (esp_timer_create(&a, &g_guard_timer) == ESP_OK) esp_timer_start_periodic(g_guard_timer, HUB_MOTOR_GUARD_PERIOD_US);
  else Serial.println("ERROR motor guard timer not created");
#ifdef PPE_TEST_HOOKS
  test_pins_start();
#endif
  xTaskCreate(motor_task, "ppe_motor", 3072, nullptr, HUB_MOTOR_TASK_PRIO, nullptr);
}

void hub_motor_set_alert(bool active) { g_alert_req = active; }
void hub_motor_set_key_fault(bool on) { g_key_req = on; }

void hub_motor_safe_off() {
  portENTER_CRITICAL(&g_mux);
  g_motor.boot();
  drive();
  portEXIT_CRITICAL(&g_mux);
}

uint32_t hub_motor_take_guard_trips() {
  portENTER_CRITICAL(&g_mux);
  uint32_t n = g_trips_unreported;
  g_trips_unreported = 0;
  portEXIT_CRITICAL(&g_mux);
  return n;
}

void hub_motor_info(HubMotorInfo* i) {
  portENTER_CRITICAL(&g_mux);
  i->state = (uint8_t)g_motor.state(); i->out = g_motor.output(); i->alert = g_motor.alert(); i->key_fault = g_motor.key_fault();
  i->pulse = g_motor.pulse(); i->guard_trips = g_motor.guard_trips(); i->max_on_us = g_motor.max_on_us();
  portEXIT_CRITICAL(&g_mux);
}

#ifdef PPE_TEST_HOOKS
void hub_hook_motor() {
  static const char* const kName[] = {"IDLE", "ON", "OFF", "K_ON", "K_OFF", "K_REST"};
  HubMotorInfo i;
  hub_motor_info(&i);
  Serial.printf("MOTOR pin=%d state=%s out=%d alert=%d key_fault=%d pulse=%u guard_trips=%lu max_on_us=%lu t_ms=%lu\n", (int)HUB_MOTOR_PIN,
                i.state < 6 ? kName[i.state] : "?", i.out, i.alert, i.key_fault, i.pulse, (unsigned long)i.guard_trips,
                (unsigned long)i.max_on_us, (unsigned long)(esp_timer_get_time() / 1000));
}
#endif
