// ppe_node.h - node identity and packet builder (state machines N-01, T-03). Pure logic: no Arduino, no NVS, no radio, no heap.
// The firmware loads the identity from NVS into a ppe_ident_t and calls ppe_build_adv() once per logical packet.
// The copies of one logical packet are the same 21 bytes (T-05, no recompute).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "ppe_proto.h"
#include "ppe_sense.h"

typedef struct {
  uint8_t node_id;               // 1..5
  uint8_t vest_id;               // 1..254
  uint8_t key[PPE_KEY_LEN];
  bool    has_key;
} ppe_ident_t;

void ppe_ident_clear(ppe_ident_t* id);
// N-01: node_id in 1..5, vest_id in 1..254 and a key present. Anything else = UNPROVISIONED, the radio is never started.
bool ppe_ident_valid(const ppe_ident_t* id);
// Confirmed wear state -> wire state. WORN -> 0x01; NOT_WORN and NONE -> 0x00 (an event or heartbeat never carries NONE).
uint8_t ppe_wear_to_wire(ppe_wear_t s);
// T-03: flags (VER=1, TYPE, BOOT, LOW_BATT from battery <= 20, SENSOR_FAULT), payload, HMAC, 21-byte advertising data.
// battery_pct: 0..100, or 0xFF when the battery could not be read. False for an invalid identity or a crypto failure
// (then out is untouched).
bool ppe_build_adv(const ppe_ident_t* id, ppe_pkt_type_t type, bool boot, bool sensor_fault, ppe_wear_t state,
                   uint8_t battery_pct, uint32_t counter, uint8_t out[PPE_ADV_LEN]);
