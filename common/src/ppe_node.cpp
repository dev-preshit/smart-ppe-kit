#include "ppe_node.h"
#include "ppe_crypto.h"
#include <string.h>

void ppe_ident_clear(ppe_ident_t* id) { memset(id, 0, sizeof *id); }

bool ppe_ident_valid(const ppe_ident_t* id) {
  return id && id->has_key && ppe_node_id_valid(id->node_id) && ppe_vest_id_valid(id->vest_id) && id->vest_id != 0xFFu;
}

uint8_t ppe_wear_to_wire(ppe_wear_t s) { return s == PPE_WEAR_WORN ? (uint8_t)PPE_WORN : (uint8_t)PPE_NOT_WORN; }

bool ppe_build_adv(const ppe_ident_t* id, ppe_pkt_type_t type, bool boot, bool sensor_fault, ppe_wear_t state,
                   uint8_t battery_pct, uint32_t counter, uint8_t out[PPE_ADV_LEN]) {
  if (!ppe_ident_valid(id) || !out) return false;
  ppe_packet_t p;
  memset(&p, 0, sizeof p);
  p.flags = ppe_flags_make(type, boot, ppe_low_batt(battery_pct), sensor_fault);
  p.vest_id = id->vest_id;
  p.node_id = id->node_id;
  p.state = ppe_wear_to_wire(state);
  p.battery = battery_pct;
  p.counter = counter;
  uint8_t payload[PPE_PAYLOAD_LEN];
  ppe_encode_payload(p, payload);
  if (!ppe_sign_payload(id->key, payload)) return false;
  ppe_make_adv(payload, out);
  return true;
}
