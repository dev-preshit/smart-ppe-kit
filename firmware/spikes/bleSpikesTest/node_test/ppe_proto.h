// ppe_proto.h - S0 spike packet format. IDENTICAL copy lives in node_test/ and hub_test/.
#pragma once
#include <Arduino.h>
#include <stddef.h>
#include "mbedtls/md.h"

#define PPE_COMPANY_ID  0xFFFF   // test company ID
#define PPE_PROTO_VER   1
#define PPE_MAC_LEN     8        // truncated HMAC-SHA256

// SPIKE ONLY - throwaway key, never reuse in real firmware
static const uint8_t PPE_KEY[32] = {
  0x50,0x50,0x45,0x2D,0x53,0x30,0x2D,0x53,0x50,0x49,0x4B,0x45,0x2D,0x4B,0x45,0x59,
  0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF
};

enum PpeType : uint8_t { PPE_HEARTBEAT = 1, PPE_EVENT = 2 };

#pragma pack(push, 1)
struct PpePacket {
  uint16_t company;   // first 2 bytes of manufacturer data
  uint8_t  ver;
  uint8_t  nodeId;    // 1..5
  uint8_t  type;
  uint8_t  flags;     // bit0 = worn
  uint32_t counter;
  uint8_t  mac[PPE_MAC_LEN];
};
#pragma pack(pop)
static_assert(sizeof(PpePacket) == 18, "unexpected PpePacket size");

static const char* const PPE_NODE_NAMES[] = {
  "?", "LEFT_BOOT", "RIGHT_BOOT", "LEFT_GLOVE", "RIGHT_GLOVE", "HELMET"
};

// MAC covers ver..counter (everything between company and mac)
inline void ppeComputeMac(const PpePacket& p, uint8_t out[PPE_MAC_LEN]) {
  uint8_t full[32];
  const uint8_t* start = (const uint8_t*)&p + offsetof(PpePacket, ver);
  size_t len = offsetof(PpePacket, mac) - offsetof(PpePacket, ver);
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                  PPE_KEY, sizeof(PPE_KEY), start, len, full);
  memcpy(out, full, PPE_MAC_LEN);
}

inline bool ppeVerifyMac(const PpePacket& p) {
  uint8_t calc[PPE_MAC_LEN];
  ppeComputeMac(p, calc);
  uint8_t diff = 0;                       // constant-time compare
  for (size_t i = 0; i < PPE_MAC_LEN; i++) diff |= calc[i] ^ p.mac[i];
  return diff == 0;
}
