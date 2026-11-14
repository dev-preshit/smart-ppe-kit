// ppe_config.h - node parameters in one struct so S4/S6 provisioning plugs in later (A-03).
// Bounds follow protocol spec 1.1 and FR-40: out-of-range values are rejected and nothing changes.
#pragma once
#include <stdint.h>

// Guarded (S4): ppe_proto.h defines the same bounds; whichever header comes first wins, the values are identical.
#ifndef PPE_SETTLE_MS_MIN
#define PPE_SETTLE_MS_MIN 0
#endif
#ifndef PPE_SETTLE_MS_MAX
#define PPE_SETTLE_MS_MAX 1000
#endif
#ifndef PPE_GRACE_S_MIN
#define PPE_GRACE_S_MIN 1
#endif
#ifndef PPE_GRACE_S_MAX
#define PPE_GRACE_S_MAX 120
#endif
#ifndef PPE_HEARTBEAT_S_MIN
#define PPE_HEARTBEAT_S_MIN 60
#endif
#ifndef PPE_HEARTBEAT_S_MAX
#define PPE_HEARTBEAT_S_MAX 3600
#endif

#ifndef PPE_SETTLE_MS_DEFAULT
#define PPE_SETTLE_MS_DEFAULT 0
#endif
#ifndef PPE_GRACE_S_DEFAULT
#define PPE_GRACE_S_DEFAULT 25
#endif
#ifndef PPE_HEARTBEAT_S_DEFAULT
#define PPE_HEARTBEAT_S_DEFAULT 360
#endif

typedef struct { uint16_t settle_ms; uint16_t grace_s; uint16_t heartbeat_s; } ppe_cfg_t;

void ppe_cfg_defaults(ppe_cfg_t* c);                    // compile-time defaults (-DPPE_GRACE_S_DEFAULT=.. etc. also works)
bool ppe_cfg_set_settle(ppe_cfg_t* c, long v);
bool ppe_cfg_set_grace(ppe_cfg_t* c, long v);
bool ppe_cfg_set_heartbeat(ppe_cfg_t* c, long v);
