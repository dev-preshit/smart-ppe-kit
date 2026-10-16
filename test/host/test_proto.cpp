// TC-12 (host part: FR-12 vectors) and TC-79 (NFR-10 packet size). Expected values from gen/vectors.h.
#include "ppe_test.h"
#include "ppe_proto.h"
#include "ppe_crypto.h"
#include "sha256_portable.h"
#include "gen/vectors.h"
#include <stdlib.h>

static void fill_a1(ppe_packet_t* p) {
  p->flags = ppe_flags_make(PPE_EVENT, false, false, false);
  p->vest_id = 1; p->node_id = PPE_NODE_LEFT_GLOVE; p->state = PPE_WORN; p->battery = 90; p->counter = 258;
  memset(p->hmac, 0, 8);
}

PPE_TEST(size_constants_21_bytes, "TC-79") {   // NFR-10 inspection + unit test
  CHECK_EQ(PPE_PAYLOAD_LEN, 17); CHECK_EQ(PPE_ADV_LEN, 21); CHECK(PPE_ADV_LEN <= 31);
  uint8_t pl[PPE_PAYLOAD_LEN] = {0}, adv[PPE_ADV_LEN];
  ppe_make_adv(pl, adv);
  CHECK_EQ(sizeof adv, 21);
  CHECK_EQ(adv[0], 0x14); CHECK_EQ(adv[1], 0xFF); CHECK_EQ(adv[2], 0xFF); CHECK_EQ(adv[3], 0xFF);
  CHECK_EQ(adv[0] + 1, (int)sizeof adv);   // AD length byte covers everything after itself
}

PPE_TEST(a1_vector_adv_matches_spec, "TC-12") {
  ppe_packet_t p; fill_a1(&p);
  CHECK_EQ(p.flags, 0x60);
  uint8_t pl[PPE_PAYLOAD_LEN], adv[PPE_ADV_LEN];
  ppe_encode_payload(p, pl);
  CHECK(ppe_sign_payload(V_KEY, pl));
  CHECK_MEM(pl, V_A1_PAYLOAD, 17);
  ppe_make_adv(pl, adv);
  CHECK_MEM(adv, V_A1_ADV, 21);
  CHECK_MEM(pl + 5, "\x02\x01\x00\x00", 4);          // counter 258 little-endian
  uint8_t full[32]; CHECK(ppe_hmac_sha256(V_KEY, pl, 9, full));
  CHECK_MEM(full, V_A1_FULL_HMAC, 32);               // full digest in spec Appendix A.1
  CHECK(ppe_hmac_verify(V_KEY, pl));
}

PPE_TEST(a2_vector_payload_matches_spec, "TC-12") {
  ppe_packet_t p; memset(&p, 0, sizeof p);
  p.flags = ppe_flags_make(PPE_HEARTBEAT, false, false, false);
  p.vest_id = 1; p.node_id = PPE_NODE_HELMET; p.state = PPE_NOT_WORN; p.battery = 50; p.counter = 259;
  CHECK_EQ(p.flags, 0x40);
  uint8_t pl[PPE_PAYLOAD_LEN]; ppe_encode_payload(p, pl);
  CHECK(ppe_sign_payload(V_KEY, pl));
  CHECK_MEM(pl, V_A2_PAYLOAD, 17);
}

PPE_TEST(crypto_wrapper_matches_independent_hmac, "TC-12") {
  // mbedtls wrapper vs portable oracle vs (via vectors) Python, on random inputs
  srand(12345);
  for (int i = 0; i < 2000; i++) {
    uint8_t key[32], msg[9], a[32], b[32];
    for (auto& k : key) k = (uint8_t)rand();
    for (auto& m : msg) m = (uint8_t)rand();
    CHECK(ppe_hmac_sha256(key, msg, 9, a));
    portable_hmac_sha256(key, 32, msg, 9, b);
    CHECK_MEM(a, b, 32);
  }
}

PPE_TEST(encode_decode_roundtrip, "TC-12") {
  srand(777);
  for (int i = 0; i < 5000; i++) {
    ppe_packet_t p, q; uint8_t pl[17];
    p.flags = (uint8_t)rand(); p.vest_id = (uint8_t)rand(); p.node_id = (uint8_t)rand(); p.state = (uint8_t)rand();
    p.battery = (uint8_t)rand(); p.counter = ((uint32_t)rand() << 16) ^ (uint32_t)rand();
    for (auto& h : p.hmac) h = (uint8_t)rand();
    ppe_encode_payload(p, pl); ppe_decode_payload(pl, &q);
    CHECK(q.flags == p.flags && q.vest_id == p.vest_id && q.node_id == p.node_id && q.state == p.state);
    CHECK(q.battery == p.battery && q.counter == p.counter);
    CHECK_MEM(q.hmac, p.hmac, 8);
  }
}

PPE_TEST(flag_helpers, "TC-12") {
  CHECK_EQ(ppe_flags_make(PPE_EVENT, true, true, true), 0x60 | 0x10 | 0x08 | 0x04);
  CHECK_EQ(ppe_flags_make(PPE_HEARTBEAT, false, false, false), 0x40);
  CHECK_EQ(ppe_flags_version(0x40), 1); CHECK_EQ(ppe_flags_version(0x00), 0);
  CHECK_EQ(ppe_flags_version(0x80), 2); CHECK_EQ(ppe_flags_version(0xC0), 3);
  CHECK(ppe_low_batt(20)); CHECK(!ppe_low_batt(21)); CHECK(ppe_low_batt(0)); CHECK(!ppe_low_batt(PPE_BATT_NA));
  CHECK(!ppe_node_id_valid(0) && ppe_node_id_valid(1) && ppe_node_id_valid(5) && !ppe_node_id_valid(6));
  CHECK(!ppe_vest_id_valid(0) && ppe_vest_id_valid(1) && ppe_vest_id_valid(0xFE) && !ppe_vest_id_valid(0xFF));
}

PPE_TEST(ct_equal_detects_every_byte, "TC-12") {
  uint8_t a[8], b[8];
  for (int i = 0; i < 8; i++) a[i] = b[i] = (uint8_t)(i * 31);
  CHECK(ppe_ct_equal(a, b, 8));
  for (int i = 0; i < 8; i++) for (int bit = 0; bit < 8; bit++) {
    b[i] ^= (uint8_t)(1 << bit); CHECK(!ppe_ct_equal(a, b, 8)); b[i] ^= (uint8_t)(1 << bit);
  }
  CHECK(ppe_ct_equal(a, b, 0));
}
