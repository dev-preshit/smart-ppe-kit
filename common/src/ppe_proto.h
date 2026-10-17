// ppe_proto.h - the ONE copy of the Smart PPE wire format (protocol spec v1.1).
// Pure C++11, no Arduino, no heap. Multi-byte integers are little-endian.
#pragma once
#include <stdint.h>
#include <stddef.h>

// ---- Constants (spec 1.1). [P] values are defaults, bounded at provisioning.
#define PPE_PROTO_VER          1u
#define PPE_PAYLOAD_LEN        17u   // spec 3
#define PPE_ADV_LEN            21u   // spec 2: 4 header bytes + payload
#define PPE_HMAC_LEN           8u
#define PPE_KEY_LEN            32u
#define PPE_HMAC_INPUT_LEN     9u    // payload bytes 0..8
#define PPE_RETX_COUNT         5u
#define PPE_RETX_PERIOD_MS     500u
#define PPE_COUNTER_BLOCK      64u
#define PPE_COUNTER_LIMIT      0xFFFFFFF0u  // spec 6.1.5
#define PPE_BATT_LOW_PCT       20u
#define PPE_BATT_NA            0xFFu
#define PPE_OFFLINE_MISSED     2u
#define PPE_NODE_COUNT         5u
#define PPE_FSR_WORN_MIN       1537u
#define PPE_FSR_NOT_WORN_MAX   88u

#define PPE_HEARTBEAT_S_DEFAULT 360u
#define PPE_HEARTBEAT_S_MIN     60u
#define PPE_HEARTBEAT_S_MAX     3600u
#define PPE_GRACE_S_DEFAULT     25u
#define PPE_GRACE_S_MIN         1u
#define PPE_GRACE_S_MAX         120u
#define PPE_SETTLE_MS_DEFAULT   0u
#define PPE_SETTLE_MS_MAX       1000u
#define PPE_OFFLINE_MARGIN_S_DEFAULT 60u
#define PPE_OFFLINE_MARGIN_S_MAX     300u
#define PPE_ADV_BURST_MS_DEFAULT     100u
#define PPE_ADV_INTERVAL_MS_DEFAULT  20u

// ---- Advertising data header (spec 2): len=0x14, type=0xFF, company 0xFFFF
#define PPE_AD_LEN_BYTE   0x14u
#define PPE_AD_TYPE_MSD   0xFFu
#define PPE_COMPANY_ID_LO 0xFFu
#define PPE_COMPANY_ID_HI 0xFFu

// ---- Payload offsets (spec 3)
enum {
  PPE_OFF_FLAGS = 0, PPE_OFF_VEST = 1, PPE_OFF_NODE = 2, PPE_OFF_STATE = 3,
  PPE_OFF_BATT = 4,  PPE_OFF_CTR = 5,  PPE_OFF_HMAC = 9
};

// ---- flags (spec 3.1)
#define PPE_FLAG_VER_SHIFT   6
#define PPE_FLAG_VER_MASK    0xC0u
#define PPE_FLAG_TYPE_EVENT  0x20u
#define PPE_FLAG_BOOT        0x10u
#define PPE_FLAG_LOW_BATT    0x08u
#define PPE_FLAG_SENSOR_FAULT 0x04u
#define PPE_FLAG_RESERVED    0x03u

enum ppe_pkt_type_t { PPE_HEARTBEAT = 0, PPE_EVENT = 1 };
enum ppe_state_t    { PPE_NOT_WORN = 0x00, PPE_WORN = 0x01 };  // any other wire value: drop

// ---- node_id (spec 3.2)
enum ppe_node_id_t {
  PPE_NODE_LEFT_BOOT = 1, PPE_NODE_RIGHT_BOOT = 2, PPE_NODE_LEFT_GLOVE = 3,
  PPE_NODE_RIGHT_GLOVE = 4, PPE_NODE_HELMET = 5
};

static_assert(PPE_ADV_LEN == 4u + PPE_PAYLOAD_LEN, "adv = 4 header + 17 payload (NFR-10)");
static_assert(PPE_ADV_LEN <= 31u, "legacy advertising data limit (NFR-10)");
static_assert(PPE_PAYLOAD_LEN == PPE_HMAC_INPUT_LEN + PPE_HMAC_LEN, "payload = 9 + 8");
static_assert(PPE_OFF_HMAC == PPE_HMAC_INPUT_LEN, "HMAC covers bytes 0..8");
static_assert(PPE_AD_LEN_BYTE == PPE_ADV_LEN - 1u, "AD length excludes the length byte itself");

// Decoded packet (host-order). Plain struct, never memcpy'd to the wire.
struct ppe_packet_t {
  uint8_t  flags, vest_id, node_id, state, battery;
  uint32_t counter;
  uint8_t  hmac[PPE_HMAC_LEN];
};

// ---- flag helpers
static inline uint8_t ppe_flags_make(ppe_pkt_type_t type, bool boot, bool low_batt, bool sensor_fault) {
  return (uint8_t)((PPE_PROTO_VER << PPE_FLAG_VER_SHIFT) |
                   (type == PPE_EVENT ? PPE_FLAG_TYPE_EVENT : 0u) |
                   (boot ? PPE_FLAG_BOOT : 0u) | (low_batt ? PPE_FLAG_LOW_BATT : 0u) |
                   (sensor_fault ? PPE_FLAG_SENSOR_FAULT : 0u));
}
static inline uint8_t ppe_flags_version(uint8_t f) { return (uint8_t)((f & PPE_FLAG_VER_MASK) >> PPE_FLAG_VER_SHIFT); }
static inline bool ppe_low_batt(uint8_t battery_pct) { return battery_pct <= PPE_BATT_LOW_PCT; } // 0xFF -> false
static inline bool ppe_node_id_valid(uint8_t id)  { return id >= 1u && id <= PPE_NODE_COUNT; }
static inline bool ppe_vest_id_valid(uint8_t id)  { return id >= 0x01u && id <= 0xFEu; }
static inline bool ppe_state_valid(uint8_t s)     { return s == PPE_NOT_WORN || s == PPE_WORN; }

// ---- wire encode/decode (no HMAC here; see ppe_crypto.h / ppe_sign_payload)
void ppe_put_u32le(uint8_t* p, uint32_t v);
uint32_t ppe_get_u32le(const uint8_t* p);
// Fields -> 17 bytes (hmac copied from p.hmac).
void ppe_encode_payload(const ppe_packet_t& p, uint8_t out[PPE_PAYLOAD_LEN]);
// 17 bytes -> fields. Performs NO validation.
void ppe_decode_payload(const uint8_t in[PPE_PAYLOAD_LEN], ppe_packet_t* p);
// 17-byte payload -> 21-byte advertising data (spec 2 header).
void ppe_make_adv(const uint8_t payload[PPE_PAYLOAD_LEN], uint8_t out[PPE_ADV_LEN]);
