// ppe_counter.h - node counter with NVS reserved block (spec 6.1, FR-11, FR-17).
// Storage sits behind an interface so host tests can fake NVS, including power cuts.
#pragma once
#include "ppe_proto.h"

class PpeCounterStore {
 public:
  virtual ~PpeCounterStore() {}
  virtual bool read_ceiling(uint32_t* v) = 0;   // false = read failed
  virtual bool write_ceiling(uint32_t v) = 0;   // false = write failed; must be durable on true
};

enum ppe_ctr_status_t {
  PPE_CTR_OK = 0,
  PPE_CTR_ERR_LIMIT,   // counter + 1 > 0xFFFFFFF0   -> NODE_FAULT (N-09), needs re-provisioning
  PPE_CTR_ERR_NVS      // block write/read failed    -> NODE_FAULT (N-30), clears on reset
};

class PpeNodeCounter {
 public:
  explicit PpeNodeCounter(PpeCounterStore* store) : store_(store), counter_(0), ceiling_(0), ready_(false) {}
  // Cold boot (N-04): counter = stored ceiling; ceiling advances by 64 (clamped) and is persisted
  // BEFORE any transmit. On error the node must not transmit.
  ppe_ctr_status_t cold_boot();
  // Deep-sleep wake (N-03): restore from RTC. No NVS access.
  // An image this class could not have produced (zeroed, erased, corrupt) is refused: next() then
  // returns PPE_CTR_ERR_NVS until cold_boot() succeeds. A stale but plausible image is not detectable here.
  void resume(uint32_t counter, uint32_t ceiling);
  // Reserve the next counter (T-02/T-03). Extends the block in NVS first when needed.
  // On any error the state is unchanged and *out is untouched.
  ppe_ctr_status_t next(uint32_t* out);
  uint32_t counter() const { return counter_; }   // last used value
  uint32_t ceiling() const { return ceiling_; }
 private:
  PpeCounterStore* store_;
  uint32_t counter_, ceiling_;
  bool ready_;
};
