#include "ppe_counter.h"

static uint32_t advance_ceiling(uint32_t c) {
  uint64_t n = (uint64_t)c + PPE_COUNTER_BLOCK;
  return n > PPE_COUNTER_LIMIT ? PPE_COUNTER_LIMIT : (uint32_t)n;   // clamp (V12-07)
}

ppe_ctr_status_t PpeNodeCounter::cold_boot() {
  ready_ = false;
  uint32_t stored = 0;
  if (!store_ || !store_->read_ceiling(&stored)) return PPE_CTR_ERR_NVS;  // fail-safe: cannot prove no repeat
  if (stored > PPE_COUNTER_LIMIT) stored = PPE_COUNTER_LIMIT;
  uint32_t nc = advance_ceiling(stored);
  if (nc != stored && !store_->write_ceiling(nc)) return PPE_CTR_ERR_NVS;
  counter_ = stored; ceiling_ = nc; ready_ = true;
  return PPE_CTR_OK;
}

void PpeNodeCounter::resume(uint32_t counter, uint32_t ceiling) {
  // BUG-002: accept only an image this class can produce: 64 <= ceiling <= limit and
  // ceiling - 64 <= counter <= ceiling. A zeroed, erased or corrupted RTC image leaves the
  // counter not ready, so next() returns PPE_CTR_ERR_NVS and the caller must cold_boot().
  bool ok = ceiling >= PPE_COUNTER_BLOCK && ceiling <= PPE_COUNTER_LIMIT &&
            counter <= ceiling && counter >= ceiling - PPE_COUNTER_BLOCK;
  ready_ = ok;
  if (ok) { counter_ = counter; ceiling_ = ceiling; }
}

ppe_ctr_status_t PpeNodeCounter::next(uint32_t* out) {
  if (!ready_) return PPE_CTR_ERR_NVS;                                    // never transmit without a persisted block
  if ((uint64_t)counter_ + 1u > PPE_COUNTER_LIMIT) return PPE_CTR_ERR_LIMIT;
  if ((uint64_t)counter_ + 1u > ceiling_) {                               // T-02: extend before use
    uint32_t nc = advance_ceiling(ceiling_);
    if (!store_->write_ceiling(nc)) return PPE_CTR_ERR_NVS;
    ceiling_ = nc;
  }
  counter_ += 1u;
  *out = counter_;
  return PPE_CTR_OK;
}
