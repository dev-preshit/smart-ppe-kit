#include "ppe_config.h"

void ppe_cfg_defaults(ppe_cfg_t* c) {
  c->settle_ms = PPE_SETTLE_MS_DEFAULT; c->grace_s = PPE_GRACE_S_DEFAULT; c->heartbeat_s = PPE_HEARTBEAT_S_DEFAULT;
}
bool ppe_cfg_set_settle(ppe_cfg_t* c, long v) {
  if (v < PPE_SETTLE_MS_MIN || v > PPE_SETTLE_MS_MAX) return false;
  c->settle_ms = (uint16_t)v; return true;
}
bool ppe_cfg_set_grace(ppe_cfg_t* c, long v) {
  if (v < PPE_GRACE_S_MIN || v > PPE_GRACE_S_MAX) return false;
  c->grace_s = (uint16_t)v; return true;
}
bool ppe_cfg_set_heartbeat(ppe_cfg_t* c, long v) {
  if (v < PPE_HEARTBEAT_S_MIN || v > PPE_HEARTBEAT_S_MAX) return false;
  c->heartbeat_s = (uint16_t)v; return true;
}
