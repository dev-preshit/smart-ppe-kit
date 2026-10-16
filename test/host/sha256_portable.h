// Portable SHA-256 / HMAC-SHA256, test oracle ONLY (test plan 5.1 rule 2). Never used by firmware.
#pragma once
#include <stdint.h>
#include <stddef.h>
void portable_sha256(const uint8_t* d, size_t n, uint8_t out[32]);
void portable_hmac_sha256(const uint8_t* key, size_t klen, const uint8_t* m, size_t mlen, uint8_t out[32]);
