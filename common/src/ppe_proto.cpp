#include "ppe_proto.h"

void ppe_put_u32le(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v);       p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
uint32_t ppe_get_u32le(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
void ppe_encode_payload(const ppe_packet_t& p, uint8_t out[PPE_PAYLOAD_LEN]) {
  out[PPE_OFF_FLAGS] = p.flags; out[PPE_OFF_VEST] = p.vest_id; out[PPE_OFF_NODE] = p.node_id;
  out[PPE_OFF_STATE] = p.state; out[PPE_OFF_BATT] = p.battery;
  ppe_put_u32le(out + PPE_OFF_CTR, p.counter);
  for (size_t i = 0; i < PPE_HMAC_LEN; i++) out[PPE_OFF_HMAC + i] = p.hmac[i];
}
void ppe_decode_payload(const uint8_t in[PPE_PAYLOAD_LEN], ppe_packet_t* p) {
  p->flags = in[PPE_OFF_FLAGS]; p->vest_id = in[PPE_OFF_VEST]; p->node_id = in[PPE_OFF_NODE];
  p->state = in[PPE_OFF_STATE]; p->battery = in[PPE_OFF_BATT];
  p->counter = ppe_get_u32le(in + PPE_OFF_CTR);
  for (size_t i = 0; i < PPE_HMAC_LEN; i++) p->hmac[i] = in[PPE_OFF_HMAC + i];
}
void ppe_make_adv(const uint8_t payload[PPE_PAYLOAD_LEN], uint8_t out[PPE_ADV_LEN]) {
  out[0] = PPE_AD_LEN_BYTE; out[1] = PPE_AD_TYPE_MSD;
  out[2] = PPE_COMPANY_ID_LO; out[3] = PPE_COMPANY_ID_HI;
  for (size_t i = 0; i < PPE_PAYLOAD_LEN; i++) out[4 + i] = payload[i];
}
