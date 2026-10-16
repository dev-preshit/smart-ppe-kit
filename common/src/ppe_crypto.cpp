#include "ppe_crypto.h"
#include "mbedtls/md.h"

bool ppe_hmac_sha256(const uint8_t key[PPE_KEY_LEN], const uint8_t* msg, size_t len, uint8_t out[32]) {
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!info || !key || !msg || !out) return false;
  return mbedtls_md_hmac(info, key, PPE_KEY_LEN, msg, len, out) == 0;
}
bool ppe_hmac_tag(const uint8_t key[PPE_KEY_LEN], const uint8_t payload[PPE_PAYLOAD_LEN], uint8_t tag[PPE_HMAC_LEN]) {
  uint8_t full[32];
  if (!ppe_hmac_sha256(key, payload, PPE_HMAC_INPUT_LEN, full)) return false;
  for (size_t i = 0; i < PPE_HMAC_LEN; i++) tag[i] = full[i];
  for (size_t i = 0; i < sizeof full; i++) { volatile uint8_t* v = &full[i]; *v = 0; }  // wipe
  return true;
}
bool ppe_ct_equal(const uint8_t* a, const uint8_t* b, size_t n) {
  volatile uint8_t diff = 0;
  for (size_t i = 0; i < n; i++) diff |= (uint8_t)(a[i] ^ b[i]);
  return diff == 0;
}
bool ppe_hmac_verify(const uint8_t key[PPE_KEY_LEN], const uint8_t payload[PPE_PAYLOAD_LEN]) {
  uint8_t tag[PPE_HMAC_LEN];
  if (!ppe_hmac_tag(key, payload, tag)) return false;
  return ppe_ct_equal(tag, payload + PPE_OFF_HMAC, PPE_HMAC_LEN);
}
bool ppe_sign_payload(const uint8_t key[PPE_KEY_LEN], uint8_t payload[PPE_PAYLOAD_LEN]) {
  return ppe_hmac_tag(key, payload, payload + PPE_OFF_HMAC);
}
