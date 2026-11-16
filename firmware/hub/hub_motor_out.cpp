#include "hub_motor_out.h"
#include "esp_timer.h"
#include "src/hub_config.h"

static HubMotor g_motor;
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool g_alert_req = false;
static volatile uint32_t g_trips_unreported = 0;
static esp_timer_handle_t g_guard_timer = nullptr;

static inline uint32_t now_us() { return (uint32_t)esp_timer_get_time(); }
static inline void drive() { digitalWrite(HUB_MOTOR_PIN, g_motor.output() ? HIGH : LOW); }   // call with g_mux held

void hub_motor_early_init() {
  pinMode(HUB_MOTOR_PIN, OUTPUT);
  digitalWrite(HUB_MOTOR_PIN, LOW);            // B-01: low as early as the firmware can; the external pull-down covers the time before
}

static void motor_task(void*) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&last, 1);                 // 1 tick = 1 ms (arduino-esp32: CONFIG_FREERTOS_HZ = 1000)
    const uint32_t t = now_us();
    const bool want = g_alert_req;
    portENTER_CRITICAL(&g_mux);
    g_motor.set_alert(want, t);                // M-01 / M-06
    g_motor.pattern_step(t);                   // M-02 / M-03 / M-04
    drive();
    portEXIT_CRITICAL(&g_mux);
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

void hub_motor_start() {
  esp_timer_create_args_t a = {};
  a.callback = guard_cb;
  a.name = "ppe_guard";
  a.dispatch_method = ESP_TIMER_TASK;
  if (esp_timer_create(&a, &g_guard_timer) == ESP_OK) esp_timer_start_periodic(g_guard_timer, HUB_MOTOR_GUARD_PERIOD_US);
  else Serial.println("ERROR motor guard timer not created");
  xTaskCreate(motor_task, "ppe_motor", 3072, nullptr, HUB_MOTOR_TASK_PRIO, nullptr);
}

void hub_motor_set_alert(bool active) { g_alert_req = active; }

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
