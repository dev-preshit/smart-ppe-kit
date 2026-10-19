// Fake NVS for hub host tests: in-memory last_counter per node, with injectable failures.
#pragma once
#include "hub_pipeline.h"

class FakeHubStore : public HubStore {
 public:
  FakeHubStore() { for (int i = 0; i < 8; i++) { has[i] = false; val[i] = 0; } read_error = false; fail_writes = 0; writes = 0; write_fails = 0; }
  HubStoreResult read_last_counter(uint8_t id, uint32_t* v) override {
    if (read_error) return HUB_STORE_ERROR;
    if (id > 7 || !has[id]) return HUB_STORE_NOT_FOUND;
    *v = val[id]; return HUB_STORE_OK;
  }
  bool write_last_counter(uint8_t id, uint32_t v) override {
    if (fail_writes > 0) { fail_writes--; write_fails++; return false; }
    if (id > 7) return false;
    has[id] = true; val[id] = v; writes++; return true;
  }
  bool has[8]; uint32_t val[8];
  bool read_error; int fail_writes; unsigned writes, write_fails;
};
