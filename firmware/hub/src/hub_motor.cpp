#include "hub_motor.h"
#include "hub_config.h"

static inline bool due(uint32_t now, uint32_t deadline) { return (int32_t)(now - deadline) >= 0; }

HubMotor::HubMotor() { boot(); }

void HubMotor::boot() {
  out_ = false; alert_ = false; key_ = false; dl_valid_ = false; guard_armed_ = false; trip_pending_ = false;
  state_ = M_IDLE; n_ = 0; dl_ = 0; on_since_ = 0; guard_dl_ = 0; trips_ = 0; max_on_ = 0;
}

void HubMotor::set_out(bool high, uint32_t now_us) {
  if (high == out_) return;
  if (high) {
    on_since_ = now_us;
    guard_dl_ = now_us + HUB_MOTOR_GUARD_US;       // M-01/M-03/M-09/M-11/M-13: arm the guard with every ON edge
    guard_armed_ = true;
  } else {
    uint32_t len = now_us - on_since_;
    if (len > max_on_) max_on_ = len;
    guard_armed_ = false;                          // M-02/M-10: cancel the guard
  }
  out_ = high;
}

void HubMotor::go_on(State s, uint32_t now_us, uint32_t period_us) {
  set_out(true, now_us);
  state_ = s;
  dl_ = now_us + period_us;
  dl_valid_ = true;
}

void HubMotor::set_alert(bool active, uint32_t now_us) {
  alert_ = active;
  if (key_) return;                                // the KEY_FAULT pattern owns the motor
  if (active && state_ == M_IDLE) {
    go_on(M_ON, now_us, HUB_MOTOR_ON_US);          // M-01
  } else if (!active && (state_ == M_ON || state_ == M_OFF)) {
    set_out(false, now_us);                        // M-06: low at once, timers cancelled
    state_ = M_IDLE;
    dl_valid_ = false;
  }
}

void HubMotor::set_key_fault(bool on, uint32_t now_us) {
  if (on == key_) return;
  key_ = on;
  if (on) {
    set_out(false, now_us);
    n_ = 1;
    go_on(K_ON, now_us, HUB_KEY_ON_US);            // M-09
  } else {
    set_out(false, now_us);
    state_ = M_IDLE;
    dl_valid_ = false;
    if (alert_) go_on(M_ON, now_us, HUB_MOTOR_ON_US);
  }
}

void HubMotor::pattern_step(uint32_t now_us) {
  for (int guard_loop = 0; guard_loop < 4 && dl_valid_ && due(now_us, dl_); guard_loop++) {
    uint32_t late = now_us - dl_;
    uint32_t t = (late > HUB_MOTOR_RESYNC_US) ? now_us : dl_;   // nominal edge time unless the task was held up
    switch (state_) {
      case M_ON:                                   // M-02
        set_out(false, t); state_ = M_OFF; dl_ = t + HUB_MOTOR_OFF_US; break;
      case M_OFF:                                  // M-03 / M-04
        if (alert_ && !key_) go_on(M_ON, t, HUB_MOTOR_ON_US);
        else { state_ = M_IDLE; dl_valid_ = false; }
        break;
      case K_ON:                                   // M-10
        set_out(false, t); state_ = K_OFF; dl_ = t + HUB_KEY_OFF_US; break;
      case K_OFF:                                  // M-11 / M-12
        if (n_ < HUB_KEY_PULSES) { n_++; go_on(K_ON, t, HUB_KEY_ON_US); }
        else { state_ = K_REST; dl_ = t + HUB_KEY_REST_US; }
        break;
      case K_REST:                                 // M-13
        n_ = 1; go_on(K_ON, t, HUB_KEY_ON_US); break;
      default:
        dl_valid_ = false; break;
    }
  }
}

void HubMotor::guard_step(uint32_t now_us) {
  if (!guard_armed_ || !due(now_us, guard_dl_)) return;
  set_out(false, now_us);                          // M-05: output low by the guard, not by the pattern
  trips_++;
  trip_pending_ = true;
  state_ = (state_ == K_ON) ? K_OFF : M_OFF;       // the pattern resumes at the next ON edge
  dl_ = now_us + (state_ == K_OFF ? HUB_KEY_OFF_US : HUB_MOTOR_OFF_US);
  dl_valid_ = true;
}

bool HubMotor::take_guard_trip() {
  bool t = trip_pending_;
  trip_pending_ = false;
  return t;
}

bool HubMotor::pattern_deadline(uint32_t* at_us) const {
  if (!dl_valid_) return false;
  *at_us = dl_;
  return true;
}
