#include "ppe_filter.h"
#include "ppe_crypto.h"

ppe_rx_result_t ppe_rx_filter(bool pdu_is_nonconn_adv, const uint8_t* adv, size_t adv_len,
                              uint8_t hub_vest_id, const uint8_t key[PPE_KEY_LEN], ppe_packet_t* out) {
  // 1. format
  if (!pdu_is_nonconn_adv || adv == nullptr || adv_len != PPE_ADV_LEN) return PPE_RX_DROP_LEN;
  if (adv[0] != PPE_AD_LEN_BYTE || adv[1] != PPE_AD_TYPE_MSD ||
      adv[2] != PPE_COMPANY_ID_LO || adv[3] != PPE_COMPANY_ID_HI) return PPE_RX_DROP_LEN;
  const uint8_t* pl = adv + 4;
  ppe_decode_payload(pl, out);
  // 2. version (spec 11: before the HMAC)
  if (ppe_flags_version(out->flags) != PPE_PROTO_VER) return PPE_RX_DROP_VER;
  // 3. vest
  if (out->vest_id != hub_vest_id) return PPE_RX_DROP_VEST;
  // 4. registered node (slots 1..5 are a compile-time list, FR-25)
  if (!ppe_node_id_valid(out->node_id)) return PPE_RX_DROP_NODE;
  // 5. HMAC, constant time. A crypto failure is treated as a mismatch (fail-safe).
  if (!ppe_hmac_verify(key, pl)) return PPE_RX_HMAC_FAIL;
  // 6. state value (after the HMAC, before the counter rule)
  if (!ppe_state_valid(out->state)) return PPE_RX_DROP_STATE;
  return PPE_RX_OK;
}

ppe_ctr_verdict_t ppe_counter_check(uint32_t last, uint32_t c, uint32_t* missed) {
  if (c > last) { if (missed) *missed = c - last - 1u; return PPE_CTR_ACCEPT; }
  if (c == last) return PPE_CTR_DUPLICATE;
  return PPE_CTR_REPLAY;
}
