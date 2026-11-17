// Edge recorder (S5 test tool, decision D8, alternative to the logic analyzer EQ-01). ESP32-C3, native USB (CDCOnBoot=cdc).
// Timestamps every edge of two inputs with the C3's 1 MHz clock and prints one line per edge: E <t_us> <ch> <lvl>
//   ch 0 = GPIO 4 <- hub motor pin node (GPIO 5 of the hub, BEFORE the driver)
//   ch 1 = GPIO 3 <- hub marker pin (GPIO 6 of the hub: accepted-packet marker and calibration pulses)
// Common ground between hub and recorder; 3.3 V logic only, never connect a motor or the driver output here.
// Serial commands (115200): PING -> PONG | RESET -> READY | STAT -> STAT ... | SYNC -> SYNC <t_us>
// Build: arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc tools/edge_recorder  (no --library needed)
#include <Arduino.h>
#include "esp_timer.h"
#include "driver/gpio.h"

#ifndef REC_PIN_A
#define REC_PIN_A 4
#endif
#ifndef REC_PIN_B
#define REC_PIN_B 3
#endif

struct Edge { uint32_t t_us; uint8_t ch; uint8_t lvl; };
static const uint16_t N = 1024;
static Edge g_ring[N];
static volatile uint16_t g_head = 0, g_tail = 0;
static volatile uint32_t g_total = 0, g_dropped = 0;

static inline void IRAM_ATTR push(uint8_t ch, int pin) {
  const uint32_t t = (uint32_t)esp_timer_get_time();
  const uint8_t lvl = (uint8_t)gpio_get_level((gpio_num_t)pin);
  const uint16_t h = g_head;
  const uint16_t nh = (uint16_t)((h + 1) % N);
  if (nh == g_tail) { g_dropped = g_dropped + 1; return; }
  g_ring[h].t_us = t; g_ring[h].ch = ch; g_ring[h].lvl = lvl;
  g_head = nh;
  g_total = g_total + 1;
}
static void IRAM_ATTR isr_a() { push(0, REC_PIN_A); }
static void IRAM_ATTR isr_b() { push(1, REC_PIN_B); }

static char g_cmd[32];
static uint8_t g_cmd_n = 0;

void setup() {
  Serial.begin(115200);
  pinMode(REC_PIN_A, INPUT);
  pinMode(REC_PIN_B, INPUT);
  attachInterrupt(digitalPinToInterrupt(REC_PIN_A), isr_a, CHANGE);
  attachInterrupt(digitalPinToInterrupt(REC_PIN_B), isr_b, CHANGE);
  delay(1500);
  Serial.printf("RECORDER ready a=GPIO%d b=GPIO%d\n", REC_PIN_A, REC_PIN_B);
}

static void command(const char* c) {
  if (!strcmp(c, "PING")) Serial.println("PONG");
  else if (!strcmp(c, "RESET")) { g_tail = g_head; g_total = 0; g_dropped = 0; Serial.println("READY"); }
  else if (!strcmp(c, "STAT")) Serial.printf("STAT a=%d b=%d total=%lu dropped=%lu t_us=%lu\n", digitalRead(REC_PIN_A), digitalRead(REC_PIN_B),
                                             (unsigned long)g_total, (unsigned long)g_dropped, (unsigned long)(uint32_t)esp_timer_get_time());
  else if (!strcmp(c, "SYNC")) Serial.printf("SYNC %lu\n", (unsigned long)(uint32_t)esp_timer_get_time());
}

void loop() {
  while (g_tail != g_head) {
    const Edge e = g_ring[g_tail];
    g_tail = (uint16_t)((g_tail + 1) % N);
    Serial.printf("E %lu %u %u\n", (unsigned long)e.t_us, e.ch, e.lvl);
  }
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') { if (g_cmd_n) { g_cmd[g_cmd_n] = 0; command(g_cmd); g_cmd_n = 0; } }
    else if (g_cmd_n < sizeof g_cmd - 1) g_cmd[g_cmd_n++] = ch;
  }
  delay(1);
}
