// Scan supervisor decision logic (FR-21, state machine S-01..S-08), pure C++ with an injected clock.
// The glue polls the scanner every HUB_SCAN_POLL_MS and calls tick(). "healthy" means: the scanner is
// active AND the host stack reports synced. Receiving no advertisements is NOT unhealthy (FR-21).
#pragma once
#include <stdint.h>
#include "hub_config.h"

enum HubScanAction { HUB_SCAN_NONE = 0, HUB_SCAN_RESTART = 1, HUB_SCAN_REBOOT = 2 };

struct HubScanTick {
  HubScanAction action;
  bool quiet_hit;        // a 60 s window ended with no advertisement of any kind (log only, spec 9.3)
};

class HubScanSupervisor {
 public:
  HubScanSupervisor();
  void start(uint32_t now_ms, uint32_t adv_total);
  // adv_total = callbacks seen so far (rx_total + queue_full), used only for the quiet counter.
  HubScanTick tick(uint32_t now_ms, bool healthy, uint32_t adv_total);
  // The glue reports what the restart attempt did. ok=false: the restart call itself failed
  // (counts as a failed restart at once, retried on the next tick). ok=true: the scan was restarted;
  // if it is still not healthy HUB_SCAN_STALL_MS later that restart counts as failed (S-05).
  void restart_done(uint32_t now_ms, bool ok);

  uint32_t restarts() const { return restarts_; }
  uint32_t consecutive_fails() const { return fails_; }
  uint32_t quiet_windows() const { return quiet_; }
  bool unhealthy() const { return bad_valid_; }

 private:
  bool bad_valid_;
  uint32_t bad_since_;
  bool awaiting_;
  uint32_t fails_, restarts_, quiet_;
  uint32_t win_start_, win_total_;
};
