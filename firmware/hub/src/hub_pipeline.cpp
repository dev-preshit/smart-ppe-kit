#include "hub_pipeline.h"
#include <string.h>
#include "ppe_proto.h"
#include "ppe_filter.h"

HubStartResult hub_startup_check(uint8_t vest_id, bool key_present) {
  if (!ppe_vest_id_valid(vest_id)) return HUB_START_BAD_VEST;
  if (!key_present) return HUB_START_NO_KEY;
  return HUB_START_OK;
}

HubPipeline::HubPipeline()
    : vest_id_(0), key_ok_(false), registry_ok_(false), accept_enabled_(false) {
  memset(key_, 0, sizeof key_);
  memset(&g_, 0, sizeof g_);
  memset(node_, 0, sizeof node_);
  memset(&dummy_, 0, sizeof dummy_);
}

bool HubPipeline::init(uint8_t vest_id, const uint8_t* key32) {
  key_ok_ = false;
  accept_enabled_ = false;
  if (hub_startup_check(vest_id, key32 != nullptr) != HUB_START_OK) return false;
  vest_id_ = vest_id;
  memcpy(key_, key32, sizeof key_);
  key_ok_ = true;
  return true;
}

bool HubPipeline::load_registry(HubStore* store, uint32_t boot_ms) {
  memset(&g_, 0, sizeof g_);
  memset(node_, 0, sizeof node_);
  registry_ok_ = (store != nullptr);
  for (uint8_t id = 1; id <= HUB_NODE_SLOTS; id++) {
    HubNode& n = node_[id];
    n.state = HUB_NODE_UNKNOWN;       // spec: states are UNKNOWN after every start (TC-25)
    n.last_seen_ms = boot_ms;
    if (!store) continue;
    uint32_t v = 0;
    HubStoreResult rc = store->read_last_counter(id, &v);
    if (rc == HUB_STORE_OK) n.last_counter = v;
    else if (rc == HUB_STORE_NOT_FOUND) n.last_counter = 0;   // fresh hub, spec 6.2 initial L = 0
    else registry_ok_ = false;                                // unreadable: fail safe
  }
  accept_enabled_ = key_ok_ && registry_ok_;
  return registry_ok_;
}

const HubNode& HubPipeline::node(uint8_t id) const {
  if (id < 1 || id > HUB_NODE_SLOTS) return dummy_;   // all-zero slot, never written
  return node_[id];
}

HubResult HubPipeline::process(const uint8_t* adv, size_t len, bool pdu_nonconn, uint32_t now_ms) {
  HubResult r;
  memset(&r, 0, sizeof r);
  g_.rx_total++;
  if (!accept_enabled_) { r.verdict = HUB_V_DROP_DISABLED; return r; }

  ppe_packet_t p;
  memset(&p, 0, sizeof p);
  switch (ppe_rx_filter(pdu_nonconn, adv, len, vest_id_, key_, &p)) {
    case PPE_RX_OK: break;
    case PPE_RX_DROP_LEN:  g_.drop_len++;  r.verdict = HUB_V_DROP_LEN;  return r;
    case PPE_RX_DROP_VER:  g_.drop_ver++;  r.verdict = HUB_V_DROP_VER;  return r;
    case PPE_RX_DROP_VEST: g_.drop_vest++; r.verdict = HUB_V_DROP_VEST; return r;
    case PPE_RX_DROP_NODE: g_.drop_node++; r.verdict = HUB_V_DROP_NODE; return r;
    case PPE_RX_HMAC_FAIL:
      node_[p.node_id].c.hmac_fail++;      // node id was range-checked at step 4
      r.verdict = HUB_V_HMAC_FAIL; r.node_id = p.node_id; r.counter = p.counter;
      return r;
    case PPE_RX_DROP_STATE:
      g_.drop_state++;
      r.verdict = HUB_V_DROP_STATE; r.node_id = p.node_id; r.counter = p.counter;
      return r;
    default: g_.drop_len++; r.verdict = HUB_V_DROP_LEN; return r;   // unknown code: fail safe, drop
  }

  HubNode& n = node_[p.node_id];
  r.node_id = p.node_id;
  r.counter = p.counter;
  r.last_counter = n.last_counter;
  uint32_t missed = 0;
  switch (ppe_counter_check(n.last_counter, p.counter, &missed)) {
    case PPE_CTR_ACCEPT:
      n.last_counter = p.counter;                    // RAM first; HubPersist writes NVS after dispatch
      n.last_seen_ms = now_ms;                       // once per transmission: only the accepted copy
      n.state = (p.state == PPE_WORN) ? HUB_NODE_WORN : HUB_NODE_NOT_WORN;
      n.battery = p.battery;
      n.flags = p.flags;
      n.c.accepted++;
      n.c.gap_count += missed;
      r.verdict = HUB_V_ACCEPT; r.missed = missed;
      r.state = p.state; r.battery = p.battery; r.flags = p.flags;
      break;
    case PPE_CTR_DUPLICATE:
      n.c.dup_count++;                               // silent; last_seen unchanged (spec 6.2, P-11)
      r.verdict = HUB_V_DUPLICATE;
      break;
    case PPE_CTR_REPLAY:
    default:
      n.c.replay_count++;
      r.verdict = HUB_V_REPLAY;
      if (!n.anomaly_valid || (uint32_t)(now_ms - n.anomaly_ms) >= HUB_ANOMALY_LIMIT_MS) {
        n.anomaly_valid = true;
        n.anomaly_ms = now_ms;
        r.anomaly_report = true;
      }
      break;
  }
  return r;
}

const char* hub_node_state_name(uint8_t s) {
  switch (s) {
    case HUB_NODE_UNKNOWN: return "UNKNOWN";
    case HUB_NODE_NOT_WORN: return "NOT_WORN";
    case HUB_NODE_WORN: return "WORN";
    default: return "?";
  }
}

const char* hub_verdict_name(HubVerdict v) {
  switch (v) {
    case HUB_V_ACCEPT: return "ACCEPT";
    case HUB_V_DUPLICATE: return "DUPLICATE";
    case HUB_V_REPLAY: return "REPLAY";
    case HUB_V_DROP_DISABLED: return "DROP_DISABLED";
    case HUB_V_DROP_LEN: return "DROP_LEN";
    case HUB_V_DROP_VER: return "DROP_VER";
    case HUB_V_DROP_VEST: return "DROP_VEST";
    case HUB_V_DROP_NODE: return "DROP_NODE";
    case HUB_V_HMAC_FAIL: return "HMAC_FAIL";
    case HUB_V_DROP_STATE: return "DROP_STATE";
    default: return "?";
  }
}
