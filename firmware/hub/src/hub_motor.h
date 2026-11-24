// Motor pattern FSM (state machines 2.6, M-01..M-13) and its independent guard (M-05). Pure C++, no Arduino:
// the caller passes the time in microseconds and applies output() to the pin. Wrap safe (32 bit).
//   pattern_step(now)  the pattern task: ON/OFF edges (M-02, M-03, M-04, M-10..M-13)
//   guard_step(now)    the independent guard: forces the output low 400 ms after an ON edge even when the pattern task
//                      is stuck (M-05). It never needs the pattern state to act.
// alert source: HubPipeline::alert_active() (D-10). key_fault is set at boot only (B-05c) and owns the motor.
#pragma once
#include <stdint.h>

class HubMotor {
 public:
  enum State { M_IDLE = 0, M_ON, M_OFF, K_ON, K_OFF, K_REST };
  HubMotor();
  void boot();                                      // M-07 / M-08 / B-01: output low, idle, everything cleared
  void set_alert(bool active, uint32_t now_us);     // M-01 (start now) and M-06 (stop at once)
  void set_key_fault(bool on, uint32_t now_us);     // M-09: KEY_FAULT pattern at boot; M-01 and M-03 never start while set
  void pattern_step(uint32_t now_us);
  void guard_step(uint32_t now_us);

  bool output() const { return out_; }
  State state() const { return state_; }
  bool alert() const { return alert_; }
  bool key_fault() const { return key_; }
  uint8_t pulse() const { return n_; }
  uint32_t guard_trips() const { return trips_; }
  bool take_guard_trip();                           // true once per guard trip: the caller reports HUB_FAULT (GUARD)
  uint32_t max_on_us() const { return max_on_; }    // longest continuous ON seen (NFR-05, TC-74)
  bool pattern_deadline(uint32_t* at_us) const;     // next pattern edge, for a scheduler

 private:
  bool out_, alert_, key_, dl_valid_, guard_armed_, trip_pending_;
  State state_;
  uint8_t n_;
  uint32_t dl_, on_since_, guard_dl_, trips_, max_on_;
  void set_out(bool high, uint32_t now_us);
  void go_on(State s, uint32_t now_us, uint32_t period_us);
};
