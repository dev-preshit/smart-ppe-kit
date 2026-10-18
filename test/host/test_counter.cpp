// Counter logic behind the storage interface. Host part of TC-11 (step 5), TC-75 (NFR-06) and TC-17 (counter log never repeats).
#include "ppe_test.h"
#include "ppe_counter.h"
#include <stdlib.h>

struct FakeNvs : PpeCounterStore {
  uint32_t value = 0; bool has = true; unsigned writes = 0;
  bool fail_read = false, fail_write = false;
  bool dead = false; int cut_in = -1; bool commit_on_cut = false;   // power-cut model
  bool read_ceiling(uint32_t* v) override { if (dead || fail_read || !has) return false; *v = value; return true; }
  bool write_ceiling(uint32_t v) override {
    if (dead || fail_write) return false;
    if (cut_in == 0) { dead = true; if (commit_on_cut) { value = v; writes++; } return false; }
    if (cut_in > 0) cut_in--;
    value = v; writes++; return true;
  }
};

PPE_TEST(first_packet_after_provisioning_is_baseline_plus_one, "TC-11") {
  FakeNvs n; n.value = 0; PpeNodeCounter c(&n); uint32_t x;
  CHECK_EQ(c.cold_boot(), PPE_CTR_OK); CHECK_EQ(n.value, 64);
  CHECK_EQ(c.next(&x), PPE_CTR_OK); CHECK_EQ(x, 1);                  // hub L=0 accepts c=1
}

PPE_TEST(increments_by_one_and_one_write_per_64, "TC-11") {
  FakeNvs n; PpeNodeCounter c(&n); uint32_t x, prev = 0;
  CHECK_EQ(c.cold_boot(), PPE_CTR_OK);
  for (unsigned i = 1; i <= 640; i++) {
    CHECK_EQ(c.next(&x), PPE_CTR_OK); CHECK_EQ(x, prev + 1); prev = x;
    CHECK(n.writes <= i / 64 + 1);                                    // at most 1 write per 64 packets (+ boot)
  }
  CHECK_EQ(n.writes, 10);
}

PPE_TEST(million_packets_fake_nvs, "TC-11") {
  FakeNvs n; PpeNodeCounter c(&n); uint32_t x, prev = 0; bool ok = true;
  CHECK_EQ(c.cold_boot(), PPE_CTR_OK);
  for (unsigned i = 1; i <= 1000000; i++) {
    if (c.next(&x) != PPE_CTR_OK || x != prev + 1) { ok = false; break; }
    prev = x;
  }
  CHECK(ok); CHECK_EQ(prev, 1000000); CHECK(n.writes <= 1000000 / 64 + 2);
  CHECK(n.value >= prev);                                             // persisted ceiling always covers used values
}

PPE_TEST(hundred_forced_resets_never_repeat_or_decrease, "TC-11") {
  FakeNvs n; uint32_t x, last_first = 0;
  for (int r = 0; r < 100; r++) {
    PpeNodeCounter c(&n); CHECK_EQ(c.cold_boot(), PPE_CTR_OK);
    CHECK_EQ(c.next(&x), PPE_CTR_OK); CHECK(x > last_first); last_first = x;
    for (int k = 0; k < r % 7; k++) c.next(&x);
  }
}

PPE_TEST(deep_sleep_resume_makes_no_nvs_write, "TC-11") {
  FakeNvs n; PpeNodeCounter c(&n); uint32_t x;
  c.resume(100, 128);
  for (int i = 0; i < 28; i++) CHECK_EQ(c.next(&x), PPE_CTR_OK);
  CHECK_EQ(x, 128); CHECK_EQ(n.writes, 0);                            // no write while inside the block
  CHECK_EQ(c.next(&x), PPE_CTR_OK); CHECK_EQ(x, 129); CHECK_EQ(n.writes, 1); CHECK_EQ(n.value, 192);
}

PPE_TEST(limit_clamp_and_counter_limit_fault, "TC-17") {
  FakeNvs n; n.value = PPE_COUNTER_LIMIT - 10; PpeNodeCounter c(&n); uint32_t x = 0;
  CHECK_EQ(c.cold_boot(), PPE_CTR_OK); CHECK_EQ(c.ceiling(), PPE_COUNTER_LIMIT);
  for (int i = 0; i < 10; i++) CHECK_EQ(c.next(&x), PPE_CTR_OK);
  CHECK_EQ(x, PPE_COUNTER_LIMIT);
  unsigned w = n.writes;
  CHECK_EQ(c.next(&x), PPE_CTR_ERR_LIMIT); CHECK_EQ(c.next(&x), PPE_CTR_ERR_LIMIT);
  CHECK_EQ(x, PPE_COUNTER_LIMIT); CHECK_EQ(n.writes, w);              // state unchanged, no write
  PpeNodeCounter d(&n);                                               // reset: limit fault returns (needs re-provisioning)
  CHECK_EQ(d.cold_boot(), PPE_CTR_OK); CHECK_EQ(d.next(&x), PPE_CTR_ERR_LIMIT);
}

PPE_TEST(nvs_write_failure_means_no_counter_issued, "TC-17") {
  FakeNvs n; PpeNodeCounter c(&n); uint32_t x = 0;
  CHECK_EQ(c.cold_boot(), PPE_CTR_OK);
  for (int i = 0; i < 64; i++) CHECK_EQ(c.next(&x), PPE_CTR_OK);      // block exhausted
  n.fail_write = true; x = 0;
  CHECK_EQ(c.next(&x), PPE_CTR_ERR_NVS); CHECK_EQ(x, 0); CHECK_EQ(c.counter(), 64);
  CHECK_EQ(c.next(&x), PPE_CTR_ERR_NVS);                              // still refuses
  n.fail_write = false;
  CHECK_EQ(c.next(&x), PPE_CTR_OK); CHECK_EQ(x, 65);                  // clears when storage recovers
}

PPE_TEST(cold_boot_nvs_failures_block_transmit, "TC-17") {
  uint32_t x;
  { FakeNvs n; n.fail_write = true; PpeNodeCounter c(&n); CHECK_EQ(c.cold_boot(), PPE_CTR_ERR_NVS); CHECK_EQ(c.next(&x), PPE_CTR_ERR_NVS); }
  { FakeNvs n; n.fail_read = true;  PpeNodeCounter c(&n); CHECK_EQ(c.cold_boot(), PPE_CTR_ERR_NVS); CHECK_EQ(c.next(&x), PPE_CTR_ERR_NVS); }
  { PpeNodeCounter c(nullptr); CHECK_EQ(c.cold_boot(), PPE_CTR_ERR_NVS); }
  { FakeNvs n; PpeNodeCounter c(&n); CHECK_EQ(c.next(&x), PPE_CTR_ERR_NVS); }   // never booted
}

// 1000 random power cuts: each cut kills power at a random NVS operation (with or without the write
// reaching flash) or at a random point between packets. After every restart the first counter must be
// strictly greater than every counter issued before (TC-75 pass rule, NFR-06).
PPE_TEST(thousand_simulated_power_cuts_counter_strictly_increasing, "TC-75") {
  FakeNvs n; uint64_t s = 0x9E3779B97F4A7C15ull;
  auto rnd = [&s]() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (uint32_t)(s >> 11); };
  uint32_t max_ever = 0; int cuts = 0, nvs_cut_commit = 0, nvs_cut_lost = 0, ram_cut = 0, boot_cut = 0, bad = 0;
  while (cuts < 1000) {
    n.dead = false; n.cut_in = -1;
    bool nvs_mode = (rnd() % 3) != 0;
    if (nvs_mode) { n.cut_in = (int)(rnd() % 4); n.commit_on_cut = (rnd() & 1) != 0; }
    unsigned budget = rnd() % 300;                      // packets before a RAM-only cut
    PpeNodeCounter c(&n);                               // RAM state is lost on every power cycle
    bool cut = false; uint32_t x; bool have_first = false, prev_ok = false; uint32_t prev = 0;
    if (c.cold_boot() != PPE_CTR_OK) { cut = n.dead; if (n.dead) boot_cut++; }
    else {
      for (unsigned i = 0; i < budget + 200 && !cut; i++) {
        ppe_ctr_status_t st = c.next(&x);
        if (st != PPE_CTR_OK) { cut = n.dead; if (n.dead) { if (n.commit_on_cut) nvs_cut_commit++; else nvs_cut_lost++; } break; }
        if (!have_first) { have_first = true; if (x <= max_ever) bad++; }
        if (prev_ok && x != prev + 1) bad++;
        prev = x; prev_ok = true; if (x > max_ever) max_ever = x;
        if (!nvs_mode && i + 1 >= budget) { cut = true; ram_cut++; }   // power lost between packets
      }
    }
    if (cut) cuts++;
  }
  CHECK_EQ(bad, 0); CHECK_EQ(cuts, 1000);
  CHECK(nvs_cut_commit > 20 && nvs_cut_lost > 20 && ram_cut > 20);   // all cut kinds were exercised
  fprintf(stderr, "  power cuts: commit=%d lost=%d ram=%d boot=%d max=%u\n", nvs_cut_commit, nvs_cut_lost, ram_cut, boot_cut, max_ever);
}

// BUG-002 / F-06: resume() must refuse an RTC image that this class could never have produced.
// Every image cold_boot()/next() can produce satisfies: 64 <= ceiling <= PPE_COUNTER_LIMIT and
// ceiling - 64 <= counter <= ceiling. Anything else (zeroed or erased RTC, corruption) must make
// next() fail with PPE_CTR_ERR_NVS, issue no counter and leave NVS untouched.
PPE_TEST(resume_refuses_implausible_rtc_image, "TC-75") {
  struct Img { uint32_t counter, ceiling; } bad[] = {
    {0, 0},                                   // zeroed RTC memory
    {10, 5},                                  // counter above ceiling
    {0, 128},                                 // counter below the start of its block
    {5, PPE_COUNTER_LIMIT + 1u},              // ceiling above the limit
    {0xFFFFFFFFu, 0xFFFFFFFFu}                // erased memory
  };
  for (const Img& b : bad) {
    FakeNvs n; n.value = 5000; PpeNodeCounter c(&n); uint32_t x = 777;
    c.resume(b.counter, b.ceiling);
    CHECK_EQ(c.next(&x), PPE_CTR_ERR_NVS);
    CHECK_EQ(x, 777); CHECK_EQ(n.writes, 0); CHECK_EQ(n.value, 5000);
  }
}

// Guard against over-rejection: every state the counter itself reaches, including block edges
// and the clamp near PPE_COUNTER_LIMIT, must be accepted by resume() (spec 6.1 deep-sleep wake).
PPE_TEST(resume_accepts_every_image_the_counter_can_produce, "TC-75") {
  uint32_t bases[] = {0, 5000, PPE_COUNTER_LIMIT - 200};
  for (uint32_t base : bases) {
    FakeNvs n; n.value = base; PpeNodeCounter a(&n); uint32_t x;
    CHECK_EQ(a.cold_boot(), PPE_CTR_OK);
    for (int i = 0; i < 300; i++) {
      if (a.next(&x) != PPE_CTR_OK) break;
      FakeNvs m; m.value = n.value; PpeNodeCounter b(&m); uint32_t y = 0;
      b.resume(a.counter(), a.ceiling());
      ppe_ctr_status_t st = b.next(&y);
      CHECK(st == PPE_CTR_OK || st == PPE_CTR_ERR_LIMIT);
      if (st == PPE_CTR_OK) CHECK_EQ(y, a.counter() + 1u);
    }
  }
}

// BUG-003 / F-07: next() must not dereference a missing store when the block has to be extended.
PPE_TEST(next_with_null_store_refuses_instead_of_crashing, "TC-17") {
  PpeNodeCounter c(nullptr); uint32_t x = 777;
  c.resume(64, 64);                                   // valid image, block exhausted: next() must extend it
  CHECK_EQ(c.next(&x), PPE_CTR_ERR_NVS);
  CHECK_EQ(x, 777); CHECK_EQ(c.counter(), 64); CHECK_EQ(c.ceiling(), 64);
  PpeNodeCounter d(nullptr);                          // inside the block no store is needed
  d.resume(100, 128);
  CHECK_EQ(d.next(&x), PPE_CTR_OK); CHECK_EQ(x, 101);
}
