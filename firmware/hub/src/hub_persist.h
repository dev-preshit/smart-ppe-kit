// last_counter persistence (spec 6.2, state machine P-13), pure C++.
// The receive path calls note() after dispatch (RAM only, never blocks). A low-priority task calls
// next()/done() to write NVS, one value per node, newest wins (coalescing). A power cut before the
// write lands loses at most the last accepted counter per node; the node's next packet moves past it.
#pragma once
#include <stdint.h>
#include "hub_config.h"

class HubStore;

class HubPersist {
 public:
  HubPersist();
  void note(uint8_t node_id, uint32_t counter);          // newest value per node wins
  bool next(uint8_t* node_id, uint32_t* counter);        // pending write, false when nothing to do
  void done(uint8_t node_id, uint32_t counter, bool ok); // result of the write for that pair
  // Convenience used by host tests and by tools that do not time the write: drains everything.
  unsigned drain(HubStore* store);

  unsigned pending() const;
  uint32_t writes_ok() const { return writes_ok_; }
  uint32_t writes_failed() const { return writes_failed_; }

 private:
  bool has_[HUB_NODE_SLOTS + 1];
  uint32_t value_[HUB_NODE_SLOTS + 1];
  uint32_t writes_ok_, writes_failed_;
};
