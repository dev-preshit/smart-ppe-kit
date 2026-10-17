// ppe_filter.h - hub receive filter (spec 2, steps 1-6) and counter rule (spec 6.2, step 7).
// Pure functions: counters, NVS and registry state live in the caller (S3).
#pragma once
#include "ppe_proto.h"

enum ppe_rx_result_t {
  PPE_RX_OK = 0,        // steps 1-6 passed; run ppe_counter_check next
  PPE_RX_DROP_LEN,      // step 1: not ADV_NONCONN_IND, length != 21, bad AD header, null adv or out  (drop_len)
  PPE_RX_DROP_VER,      // step 2: version != PROTO_VER, checked BEFORE the HMAC        (drop_ver)
  PPE_RX_DROP_VEST,     // step 3                                                        (drop_vest)
  PPE_RX_DROP_NODE,     // step 4: node_id not 1..5                                      (drop_node)
  PPE_RX_HMAC_FAIL,     // step 5: per-node hmac_fail; counter/state must not be touched
  PPE_RX_DROP_STATE     // step 6: valid HMAC, state not 0x00/0x01                       (drop_state)
};

// pdu_is_nonconn_adv: decided by the BLE glue (NimBLE event type) so this core stays platform-free.
// On PPE_RX_OK, *out holds the decoded packet. On PPE_RX_DROP_LEN *out is not touched (this includes a null out).
// On every other result *out is fully decoded, but only the checks before the failing step have passed, so it
// must not be used except node_id for PPE_RX_HMAC_FAIL accounting, which the caller may only trust when
// result == PPE_RX_HMAC_FAIL.
ppe_rx_result_t ppe_rx_filter(bool pdu_is_nonconn_adv, const uint8_t* adv, size_t adv_len,
                              uint8_t hub_vest_id, const uint8_t key[PPE_KEY_LEN], ppe_packet_t* out);

enum ppe_ctr_verdict_t { PPE_CTR_ACCEPT, PPE_CTR_DUPLICATE, PPE_CTR_REPLAY };
// L = hub last_counter[n], c = packet counter. On ACCEPT, *missed = c - L - 1 (gap, not an error).
ppe_ctr_verdict_t ppe_counter_check(uint32_t last, uint32_t c, uint32_t* missed);
