#include "hub_scan_supervisor.h"

HubScanSupervisor::HubScanSupervisor()
    : bad_valid_(false), bad_since_(0), awaiting_(false), fails_(0), restarts_(0), quiet_(0),
      win_start_(0), win_total_(0) {}

void HubScanSupervisor::start(uint32_t now_ms, uint32_t adv_total) {
  bad_valid_ = false; awaiting_ = false; fails_ = 0;
  win_start_ = now_ms; win_total_ = adv_total;
}

HubScanTick HubScanSupervisor::tick(uint32_t now_ms, bool healthy, uint32_t adv_total) {
  HubScanTick t = {HUB_SCAN_NONE, false};
  if ((uint32_t)(now_ms - win_start_) >= HUB_QUIET_WINDOW_MS) {
    if (adv_total == win_total_) { quiet_++; t.quiet_hit = true; }
    win_start_ = now_ms; win_total_ = adv_total;
  }
  if (healthy) {                       // S-04: progress, forget every failure
    bad_valid_ = false; awaiting_ = false; fails_ = 0;
    return t;
  }
  if (!bad_valid_) { bad_valid_ = true; bad_since_ = now_ms; }
  if ((uint32_t)(now_ms - bad_since_) < HUB_SCAN_STALL_MS) return t;
  if (awaiting_) { fails_++; awaiting_ = false; }      // last restart showed no progress for 10 s
  if (fails_ >= HUB_SCAN_MAX_FAILS) { t.action = HUB_SCAN_REBOOT; return t; }   // S-06
  restarts_++;
  t.action = HUB_SCAN_RESTART;
  return t;
}

void HubScanSupervisor::restart_done(uint32_t now_ms, bool ok) {
  if (ok) {
    awaiting_ = true;
    bad_since_ = now_ms;               // give the scanner 10 s to show progress
  } else {
    fails_++;
    awaiting_ = false;
    bad_since_ = now_ms - HUB_SCAN_STALL_MS;   // eligible again on the next tick
  }
  bad_valid_ = true;
}
