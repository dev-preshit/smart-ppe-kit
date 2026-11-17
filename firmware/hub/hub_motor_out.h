// Motor output glue (ESP32-S3, Arduino + FreeRTOS + esp_timer). The pattern logic is src/hub_motor.* (host tested).
//   motor task  (priority 12, every 1 ms): applies the alert flag and the pattern edges to the pin
//   guard timer (esp_timer, every 1 ms, independent of the motor task): forces the pin low 400 ms after an ON edge (M-05)
// Never drive the motor from the GPIO directly: the pin only drives the transistor base (see the wiring in the handover).
#pragma once
#include <Arduino.h>
#include "src/hub_motor.h"

void hub_motor_early_init();                 // B-01: pin output LOW, first statement of setup()
void hub_motor_start();                      // motor task and guard timer
void hub_motor_set_alert(bool active);       // called after the pipeline decision (rx task after an accepted packet, tick task)
uint32_t hub_motor_take_guard_trips();       // unreported guard trips; the tick task turns them into HUB_FAULT (GUARD)

struct HubMotorInfo { uint8_t state; bool out, alert, key_fault; uint8_t pulse; uint32_t guard_trips, max_on_us; };
void hub_motor_info(HubMotorInfo* info);

#ifdef PPE_TEST_HOOKS
void hub_hook_motor();                       // prints one MOTOR line
void hub_marker_pulse();                     // rx task, at the accepted-packet point: 1 ms pulse on HUB_MARKER_PIN when marker mode is on
void hub_hook_marker(bool on);
void hub_hook_pulse(uint32_t on_ms, uint32_t off_ms, uint32_t count);   // known pulse train on HUB_MARKER_PIN, first edge after 0.5 s
#endif
