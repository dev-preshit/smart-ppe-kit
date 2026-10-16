// Host parts of TC-22 (filter order), TC-23 (136 flips), TC-24 (counter rule), TC-32 (drop counters),
// TC-73 (forged set). These TCs are owned by S3/S6; the host part here is early evidence only.
#include "ppe_test.h"
#include "ppe_filter.h"
#include "ppe_crypto.h"
#include "gen/vectors.h"

PPE_TEST(filter_defect_cases_match_generated_expectations, "TC-22") {
  for (unsigned i = 0; i < V_CASES_N; i++) {
    ppe_packet_t out; memset(&out, 0, sizeof out);
    ppe_rx_result_t r = ppe_rx_filter(V_CASES[i].nonconn != 0, V_CASES[i].data, V_CASES[i].len, 1, V_KEY, &out);
    if ((int)r != V_CASES[i].expect) fprintf(stderr, "  case %s: got %d want %d\n", V_CASES[i].name, (int)r, V_CASES[i].expect);
    CHECK_EQ((int)r, V_CASES[i].expect);
  }
}

PPE_TEST(filter_null_adv_is_drop_len, "TC-22") {
  ppe_packet_t out;
  CHECK_EQ(ppe_rx_filter(true, nullptr, 21, 1, V_KEY, &out), PPE_RX_DROP_LEN);
}

PPE_TEST(a1_accepted_and_decoded, "TC-23") {
  ppe_packet_t out;
  CHECK_EQ(ppe_rx_filter(true, V_A1_ADV, 21, 1, V_KEY, &out), PPE_RX_OK);
  CHECK_EQ(out.node_id, 3); CHECK_EQ(out.state, 1); CHECK_EQ(out.battery, 90); CHECK_EQ(out.counter, 258);
}

PPE_TEST(all_136_bit_flips_rejected, "TC-23") {
  unsigned rejected = 0;
  for (int i = 0; i < 136; i++) {
    uint8_t adv[21] = {0x14, 0xFF, 0xFF, 0xFF}; memcpy(adv + 4, V_FLIPS[i], 17);
    ppe_packet_t out;
    ppe_rx_result_t r = ppe_rx_filter(true, adv, 21, 1, V_KEY, &out);
    CHECK(r != PPE_RX_OK);
    CHECK_EQ((int)r, V_FLIP_EXPECT[i]);            // reject at the stage the spec order predicts
    if (r != PPE_RX_OK) rejected++;
  }
  CHECK_EQ(rejected, 136);
}

PPE_TEST(forged_1000_all_rejected_by_hmac, "TC-73") {
  unsigned bad = 0;
  for (unsigned i = 0; i < V_FORGED_N; i++) {
    uint8_t adv[21] = {0x14, 0xFF, 0xFF, 0xFF}; memcpy(adv + 4, V_FORGED[i], 17);
    ppe_packet_t out;
    if (ppe_rx_filter(true, adv, 21, 1, V_KEY, &out) != PPE_RX_HMAC_FAIL) bad++;
  }
  CHECK_EQ(bad, 0); CHECK(V_FORGED_N >= 990);
}

PPE_TEST(counter_rule_table, "TC-24") {
  uint32_t missed = 99;
  CHECK_EQ(ppe_counter_check(10, 11, &missed), PPE_CTR_ACCEPT); CHECK_EQ(missed, 0);
  CHECK_EQ(ppe_counter_check(10, 15, &missed), PPE_CTR_ACCEPT); CHECK_EQ(missed, 4);
  CHECK_EQ(ppe_counter_check(10, 10, &missed), PPE_CTR_DUPLICATE);
  CHECK_EQ(ppe_counter_check(10, 9, &missed), PPE_CTR_REPLAY);
  CHECK_EQ(ppe_counter_check(0, 1, &missed), PPE_CTR_ACCEPT);           // initial L = 0
  CHECK_EQ(ppe_counter_check(0, 0, &missed), PPE_CTR_DUPLICATE);
  CHECK_EQ(ppe_counter_check(0xFFFFFFF0u, 0xFFFFFFF0u, nullptr), PPE_CTR_DUPLICATE);
  CHECK_EQ(ppe_counter_check(5, 1000000, nullptr), PPE_CTR_ACCEPT);
}

// Mini hub model: 5 copies -> 1 accepted + 4 dup; 100 replays -> 0 accepted (TC-24 host part), drop counters (TC-32).
PPE_TEST(five_copies_and_100_replays, "TC-24") {
  uint32_t last = 0; unsigned acc = 0, dup = 0, rep = 0;
  for (uint32_t c = 1; c <= 120; c++) {
    for (int copy = 0; copy < 5; copy++) {
      uint32_t m; ppe_ctr_verdict_t v = ppe_counter_check(last, c, &m);
      if (v == PPE_CTR_ACCEPT) { acc++; last = c; } else if (v == PPE_CTR_DUPLICATE) dup++; else rep++;
    }
  }
  CHECK_EQ(acc, 120); CHECK_EQ(dup, 480); CHECK_EQ(rep, 0);
  for (uint32_t c = 1; c <= 100; c++) { uint32_t m; if (ppe_counter_check(last, c, &m) == PPE_CTR_REPLAY) rep++; else acc++; }
  CHECK_EQ(rep, 100); CHECK_EQ(acc, 120);
}

PPE_TEST(drop_counters_equal_injected, "TC-32") {
  unsigned cnt[7] = {0};
  struct { int n; unsigned idx; } plan[] = {{10, 1}, {7, 2}, {12, 3}, {9, 4}, {15, 5}, {6, 6}};
  for (unsigned i = 0; i < V_CASES_N; i++) (void)i;
  // Build injected packets from the generated cases: pick one representative per drop class.
  const char* rep[] = {"", "len20", "ver2", "wrong_vest", "node6", "", "state02_valid_hmac"};
  for (auto& pl : plan) {
    for (int k = 0; k < pl.n; k++) {
      ppe_packet_t out; ppe_rx_result_t r = PPE_RX_OK;
      if (pl.idx == 5) {   // bad HMAC: flip a tag bit of A.1
        uint8_t adv[21]; memcpy(adv, V_A1_ADV, 21); adv[20] ^= 1; r = ppe_rx_filter(true, adv, 21, 1, V_KEY, &out);
      } else {
        for (unsigned i = 0; i < V_CASES_N; i++) if (!strcmp(V_CASES[i].name, rep[pl.idx]))
          r = ppe_rx_filter(V_CASES[i].nonconn != 0, V_CASES[i].data, V_CASES[i].len, 1, V_KEY, &out);
      }
      cnt[r]++;
    }
  }
  CHECK_EQ(cnt[PPE_RX_DROP_LEN], 10); CHECK_EQ(cnt[PPE_RX_DROP_VER], 7); CHECK_EQ(cnt[PPE_RX_DROP_VEST], 12);
  CHECK_EQ(cnt[PPE_RX_DROP_NODE], 9); CHECK_EQ(cnt[PPE_RX_HMAC_FAIL], 15); CHECK_EQ(cnt[PPE_RX_DROP_STATE], 6);
  CHECK_EQ(cnt[PPE_RX_OK], 0);
}

// BUG-005 / F-03: a null out pointer must be refused, not dereferenced.
PPE_TEST(filter_null_out_is_drop_len, "TC-22") {
  CHECK_EQ(ppe_rx_filter(true, V_A1_ADV, 21, 1, V_KEY, nullptr), PPE_RX_DROP_LEN);
  ppe_packet_t out;
  CHECK_EQ(ppe_rx_filter(true, V_A1_ADV, 21, 1, V_KEY, &out), PPE_RX_OK);   // a normal call still works
}
