// Shared helpers for the S5 hub logic host tests (TC-26, TC-27, TC-28, TC-31). Test code only.
// Packets are built with the S1 library functions and the public Appendix A test key (V_KEY, vest 1).
#pragma once
#include <vector>
#include "ppe_proto.h"
#include "ppe_crypto.h"
#include "hub_pipeline.h"
#include "fake_hub_store.h"
#include "gen/vectors.h"

static inline void hub_adv(uint8_t node, uint8_t state, uint32_t ctr, uint8_t out[21], uint8_t battery = 90) {
  ppe_packet_t pk; memset(&pk, 0, sizeof pk);
  pk.flags = ppe_flags_make(PPE_HEARTBEAT, false, false, false);
  pk.vest_id = 1; pk.node_id = node; pk.state = state; pk.battery = battery; pk.counter = ctr;
  uint8_t payload[PPE_PAYLOAD_LEN]; ppe_encode_payload(pk, payload);
  CHECK(ppe_sign_payload(V_KEY, payload)); ppe_make_adv(payload, out);
}

// One accepted packet for `node` with `state` (PPE_WORN / PPE_NOT_WORN) at time now_ms. ctr must be new for that node.
static inline HubResult hub_send(HubPipeline& h, uint8_t node, uint8_t state, uint32_t ctr, uint32_t now_ms, uint8_t battery = 90) {
  uint8_t adv[21]; hub_adv(node, state, ctr, adv, battery);
  return h.process(adv, 21, true, now_ms);
}

static inline void hub_boot(HubPipeline& h, FakeHubStore& st, uint32_t boot_ms = 0) {
  CHECK(h.init(1, V_KEY)); CHECK(h.load_registry(&st, boot_ms));
}

static inline std::vector<HubMsg> hub_drain(HubPipeline& h) {
  std::vector<HubMsg> v; HubMsg m[HUB_MSG_QUEUE_DEPTH];
  for (;;) { uint32_t n = h.take_messages(m, HUB_MSG_QUEUE_DEPTH); if (!n) break; for (uint32_t i = 0; i < n; i++) v.push_back(m[i]); }
  return v;
}

// Tick once per second from `from_ms` up to and including `to_ms` (all in ms), like the 1 s task on the hub.
static inline void hub_run_ticks(HubPipeline& h, uint32_t from_ms, uint32_t to_ms) {
  for (uint32_t t = from_ms; t <= to_ms; t += 1000u) h.tick(t);
}
