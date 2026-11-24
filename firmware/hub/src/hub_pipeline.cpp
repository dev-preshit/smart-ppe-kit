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
    : vest_id_(0), key_ok_(false), registry_ok_(false), accept_enabled_(false),
      heartbeat_s_(HUB_HEARTBEAT_S_DEFAULT), margin_s_(HUB_MARGIN_S_DEFAULT), vest_compliant_(false),
      msg_n_(0), msg_dropped_(0) {
  memset(key_, 0, sizeof key_);
  memset(&g_, 0, sizeof g_);
  memset(node_, 0, sizeof node_);
  memset(&dummy_, 0, sizeof dummy_);
  memset(msgq_, 0, sizeof msgq_);
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

bool HubPipeline::init_no_key(uint8_t vest_id) {
  key_ok_ = false;
  accept_enabled_ = false;
  if (hub_startup_check(vest_id, true) != HUB_START_OK) return false;   // the vest id must still be valid
  vest_id_ = vest_id;
  memset(key_, 0, sizeof key_);
  return true;
}

bool HubPipeline::load_registry(HubStore* store, uint32_t boot_ms) {
  memset(&g_, 0, sizeof g_);
  memset(node_, 0, sizeof node_);
  vest_compliant_ = false;          // C-01: the initial NON_COMPLIANT at hub boot is not a transition, no message
  msg_n_ = 0;                       // a restart forgets undelivered messages (the RAM queue is lost, X-07)
  msg_dropped_ = 0;
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
      r.prev_state = n.state;
      n.last_counter = p.counter;                    // RAM first; HubPersist writes NVS after dispatch
      n.last_seen_ms = now_ms;                       // once per transmission: only the accepted copy
      n.state = (p.state == PPE_WORN) ? HUB_NODE_WORN : HUB_NODE_NOT_WORN;   // H-02, H-03, H-05, H-06, H-08, H-09, H-11, H-12
      n.battery = p.battery;
      n.flags = p.flags;
      n.c.accepted++;
      n.c.gap_count += missed;
      r.verdict = HUB_V_ACCEPT; r.missed = missed;
      r.state = p.state; r.battery = p.battery; r.flags = p.flags;
      eval_compliance(now_ms);                       // X-R1: node FSM, then compliance, then the caller reads alert_active()
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

bool HubPipeline::set_offline_timing(uint32_t heartbeat_s, uint32_t margin_s) {
  if (heartbeat_s < HUB_HEARTBEAT_S_MIN || heartbeat_s > HUB_HEARTBEAT_S_MAX || margin_s > HUB_MARGIN_S_MAX) return false;
  heartbeat_s_ = heartbeat_s;
  margin_s_ = margin_s;
  return true;
}

void HubPipeline::push_msg(const HubMsg& m) {
  if (msg_n_ >= HUB_MSG_QUEUE_DEPTH) {              // full: drop the oldest, count it (never block the processing task)
    for (uint32_t i = 1; i < msg_n_; i++) msgq_[i - 1] = msgq_[i];
    msg_n_--;
    msg_dropped_++;
  }
  msgq_[msg_n_++] = m;
}

uint8_t HubPipeline::nodes_not_ok_mask() const {
  uint8_t mask = 0;
  for (uint8_t id = 1; id <= HUB_NODE_SLOTS; id++)
    if (node_[id].state != HUB_NODE_WORN) mask = (uint8_t)(mask | (1u << id));
  return mask;
}

bool HubPipeline::alert_active() const {
  for (uint8_t id = 1; id <= HUB_NODE_SLOTS; id++)
    if (node_[id].state == HUB_NODE_NOT_WORN || node_[id].state == HUB_NODE_OFFLINE) return true;
  return false;
}

void HubPipeline::eval_compliance(uint32_t now_ms) {
  const bool now_ok = (nodes_not_ok_mask() == 0);
  if (now_ok == vest_compliant_) return;            // C-04: no change, no message
  vest_compliant_ = now_ok;
  HubMsg m;
  memset(&m, 0, sizeof m);
  m.type = HUB_MSG_COMPLIANCE;
  m.vest_id = vest_id_;
  m.compliant = now_ok ? 1 : 0;
  m.nodes_not_ok = nodes_not_ok_mask();
  m.t_ms = now_ms;
  push_msg(m);                                      // C-02 / C-03: exactly one message per vest-level change
}

uint32_t HubPipeline::tick(uint32_t now_ms) {
  const uint32_t timeout_ms = offline_timeout_s() * 1000u;
  uint32_t went_offline = 0;
  for (uint8_t id = 1; id <= HUB_NODE_SLOTS; id++) {
    HubNode& n = node_[id];
    if (n.state == HUB_NODE_OFFLINE) continue;      // H-13: no repeated NODE_OFFLINE
    // Signed difference: 32-bit wrap safe, and a last_seen stamped a moment AFTER this tick read its clock (the rx task
    // ran between the two) gives a negative age, which is "just seen", never a false OFFLINE.
    const int32_t diff = (int32_t)(now_ms - n.last_seen_ms);
    if (diff <= 0) continue;
    const uint32_t age_ms = (uint32_t)diff;
    if (age_ms <= timeout_ms) continue;             // spec 8.2: "more than" the timeout
    n.state = HUB_NODE_OFFLINE;                     // H-04 (UNKNOWN), H-07 (WORN), H-10 (NOT_WORN)
    HubMsg m;
    memset(&m, 0, sizeof m);
    m.type = HUB_MSG_NODE_OFFLINE;
    m.vest_id = vest_id_;
    m.node_id = id;
    m.last_seen_age_s = age_ms / 1000u;
    m.t_ms = now_ms;
    push_msg(m);
    went_offline++;
  }
  if (went_offline) eval_compliance(now_ms);
  return went_offline;
}

void HubPipeline::note_boot(uint32_t reset_reason, uint32_t now_ms) {
  HubMsg m;
  memset(&m, 0, sizeof m);
  m.type = HUB_MSG_HUB_BOOT;
  m.vest_id = vest_id_;
  m.reset_reason = reset_reason;
  m.t_ms = now_ms;
  push_msg(m);
}

void HubPipeline::note_fault(uint8_t fault_code, uint32_t now_ms) {
  HubMsg m;
  memset(&m, 0, sizeof m);
  m.type = HUB_MSG_HUB_FAULT;
  m.vest_id = vest_id_;
  m.fault_code = fault_code;
  m.t_ms = now_ms;
  push_msg(m);
}

uint32_t HubPipeline::take_messages(HubMsg* out, uint32_t max) {
  if (!out) return 0;
  uint32_t k = (msg_n_ < max) ? msg_n_ : max;
  for (uint32_t i = 0; i < k; i++) out[i] = msgq_[i];
  for (uint32_t i = k; i < msg_n_; i++) msgq_[i - k] = msgq_[i];
  msg_n_ -= k;
  return k;
}

const char* hub_node_state_name(uint8_t s) {
  switch (s) {
    case HUB_NODE_UNKNOWN: return "UNKNOWN";
    case HUB_NODE_NOT_WORN: return "NOT_WORN";
    case HUB_NODE_WORN: return "WORN";
    case HUB_NODE_OFFLINE: return "OFFLINE";
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
