#include "hub_persist.h"
#include "hub_pipeline.h"   // HubStore

HubPersist::HubPersist() : writes_ok_(0), writes_failed_(0) {
  for (unsigned i = 0; i <= HUB_NODE_SLOTS; i++) { has_[i] = false; value_[i] = 0; }
}

void HubPersist::note(uint8_t node_id, uint32_t counter) {
  if (node_id < 1 || node_id > HUB_NODE_SLOTS) return;
  if (has_[node_id] && (int32_t)(counter - value_[node_id]) < 0) return;   // never persist a lower value
  has_[node_id] = true;
  value_[node_id] = counter;
}

bool HubPersist::next(uint8_t* node_id, uint32_t* counter) {
  for (uint8_t i = 1; i <= HUB_NODE_SLOTS; i++) {
    if (has_[i]) { *node_id = i; *counter = value_[i]; return true; }
  }
  return false;
}

void HubPersist::done(uint8_t node_id, uint32_t counter, bool ok) {
  if (node_id < 1 || node_id > HUB_NODE_SLOTS) return;
  if (ok) writes_ok_++; else writes_failed_++;
  // Clear the pending flag only if no newer value arrived while the write was running.
  // A failed write is not retried in a loop: the RAM counter stays authoritative and the node's next
  // accepted packet queues a fresh write (failures are counted for the dump).
  if (has_[node_id] && value_[node_id] == counter) has_[node_id] = false;
}

unsigned HubPersist::drain(HubStore* store) {
  unsigned n = 0; uint8_t id; uint32_t c;
  while (next(&id, &c)) { bool ok = store->write_last_counter(id, c); done(id, c, ok); n++; }
  return n;
}

unsigned HubPersist::pending() const {
  unsigned n = 0;
  for (unsigned i = 1; i <= HUB_NODE_SLOTS; i++) if (has_[i]) n++;
  return n;
}
