// ppe_crypto.h - HMAC wrapper (protocol spec 5). mbedtls on device AND host
// (same production code both places; mbedtls_md_hmac exists in 2.x and 3.x).
#pragma once
#include "ppe_proto.h"

// Full 32-byte HMAC-SHA256. Returns true on success.
bool ppe_hmac_sha256(const uint8_t key[PPE_KEY_LEN], const uint8_t* msg, size_t len, uint8_t out[32]);
// Truncated 8-byte tag over the first 9 payload bytes (spec 5).
bool ppe_hmac_tag(const uint8_t key[PPE_KEY_LEN], const uint8_t payload[PPE_PAYLOAD_LEN], uint8_t tag[PPE_HMAC_LEN]);
// Constant-time equality: runtime depends only on n, never on the data.
bool ppe_ct_equal(const uint8_t* a, const uint8_t* b, size_t n);
// Recompute and compare all 8 bytes in constant time. False on mismatch OR crypto failure.
bool ppe_hmac_verify(const uint8_t key[PPE_KEY_LEN], const uint8_t payload[PPE_PAYLOAD_LEN]);
// Node side: stamp the tag into payload bytes 9..16 (bytes 0..8 must already be set).
bool ppe_sign_payload(const uint8_t key[PPE_KEY_LEN], uint8_t payload[PPE_PAYLOAD_LEN]);
