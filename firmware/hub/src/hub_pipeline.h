// Hub receive pipeline (spec section 2 steps 1-7, state machines P-01..P-14), pure C++.
// Uses the S1 functions ppe_rx_filter (steps 1-6) and ppe_counter_check (step 7); adds the fixed
// 5-slot registry, the diagnostic counters (spec 9.3) and the COUNTER_ANOMALY rate limit.
// No Arduino, no RTOS, no NVS driver: storage goes through HubStore so host tests can fake it.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "hub_config.h"
#include "hub_msg.h"

enum HubStoreResult { HUB_STORE_OK = 0, HUB_STORE_NOT_FOUND = 1, HUB_STORE_ERROR = 2 };

// Storage interface for last_counter (spec 6.2). NOT_FOUND means "never written" (= 0, fresh hub);
// ERROR means the store could not be read (registry unreadable, accept disabled, fail-safe).
class HubStore {
 public:
  virtual ~HubStore() {}
  virtual HubStoreResult read_last_counter(uint8_t node_id, uint32_t* value) = 0;
  virtual bool write_last_counter(uint8_t node_id, uint32_t value) = 0;
};

// Node state (spec 8.2). S5 adds OFFLINE (timeout, H-04/H-07/H-10), the vest compliance and the alert decision.
enum HubNodeState { HUB_NODE_UNKNOWN = 0, HUB_NODE_NOT_WORN = 1, HUB_NODE_WORN = 2, HUB_NODE_OFFLINE = 3 };

enum HubVerdict {
  HUB_V_ACCEPT = 0,
  HUB_V_DUPLICATE,      // step 7, c == L: silent, only dup_count
  HUB_V_REPLAY,         // step 7, c < L: replay_count, COUNTER_ANOMALY (rate limited)
  HUB_V_DROP_DISABLED,  // accept disabled (registry unreadable): only rx_total counts
  HUB_V_DROP_LEN,       // steps 1: PDU type, length, AD header
  HUB_V_DROP_VER,       // step 2
  HUB_V_DROP_VEST,      // step 3
  HUB_V_DROP_NODE,      // step 4
  HUB_V_HMAC_FAIL,      // step 5
  HUB_V_DROP_STATE      // step 6 (valid HMAC, state not 0/1)
};

struct HubGlobalCounters {   // spec 9.3
  uint32_t rx_total, drop_len, drop_ver, drop_vest, drop_node, drop_state, scan_quiet_60s;
};
struct HubNodeCounters {     // spec 9.3
  uint32_t hmac_fail, dup_count, replay_count, gap_count, accepted;
};
struct HubNode {
  uint32_t last_counter;     // spec 6.2: RAM copy, persisted by HubPersist after dispatch
  uint32_t last_seen_ms;     // set by the accepted packet (once per 5-copy transmission), boot time at start
  uint8_t  state;            // HubNodeState
  uint8_t  battery;          // last accepted packet
  uint8_t  flags;            // last accepted packet
  bool     anomaly_valid;    // rate limit bookkeeping
  uint32_t anomaly_ms;
  HubNodeCounters c;
};

struct HubResult {
  HubVerdict verdict;
  uint8_t  node_id;          // valid when verdict is ACCEPT, DUPLICATE, REPLAY, HMAC_FAIL, DROP_STATE; else 0
  uint32_t counter;          // received counter (same validity)
  uint32_t last_counter;     // L before this packet (REPLAY / DUPLICATE / ACCEPT)
  uint32_t missed;           // ACCEPT only: c - L - 1
  bool     anomaly_report;   // REPLAY only: true when this one is reported (not rate limited)
  uint8_t  state, battery, flags;   // ACCEPT only
  uint8_t  prev_state;       // ACCEPT only: HubNodeState before this packet (H-02..H-12)
};

enum HubStartResult { HUB_START_OK = 0, HUB_START_BAD_VEST = 1, HUB_START_NO_KEY = 2 };
// Start-up rule (brief story 4): refuse to run with vest id 0x00 or 0xFF (valid 0x01..0xFE) or no key.
HubStartResult hub_startup_check(uint8_t vest_id, bool key_present);

class HubPipeline {
 public:
  HubPipeline();
  // Validates the provisioning data. Returns false (and stays disabled) on a bad vest id or null key.
  bool init(uint8_t vest_id, const uint8_t* key32);
  bool init_no_key(uint8_t vest_id);       // B-04: no key, accept stays disabled, nodes still get slots and timers
  bool key_ok() const { return key_ok_; }
  // Loads last_counter per node from the store; resets states to UNKNOWN, counters to 0,
  // last_seen to boot_ms. Returns false if the store was unreadable (accept stays disabled).
  bool load_registry(HubStore* store, uint32_t boot_ms);
  // Steps 1-7. adv = raw advertising bytes as received, pdu_nonconn = PDU is ADV_NONCONN_IND.
  // Counts rx_total for every call. Does NOT persist: the caller hands ACCEPT results to HubPersist.
  HubResult process(const uint8_t* adv, size_t len, bool pdu_nonconn, uint32_t now_ms);

  // ---- S5 hub logic (FR-26, FR-27, FR-28, FR-29 decision, FR-31) --------------------------------------------
  // OFFLINE_TIMEOUT_S = HUB_OFFLINE_MISSED x HEARTBEAT_S + OFFLINE_MARGIN_S (FR-27, FR-41). Defaults 360 / 60 = 780 s.
  // Returns false and changes nothing when a value is out of bounds (heartbeat 60..3600, margin 0..300).
  bool set_offline_timing(uint32_t heartbeat_s, uint32_t margin_s);
  uint32_t heartbeat_s() const { return heartbeat_s_; }
  uint32_t margin_s() const { return margin_s_; }
  uint32_t offline_timeout_s() const { return HUB_OFFLINE_MISSED * heartbeat_s_ + margin_s_; }
  // Called every HUB_TICK_MS (spec 8.2). A node with no accepted packet for MORE than the timeout becomes OFFLINE
  // (once: no repeated NODE_OFFLINE, H-13). Works with accept disabled too (B-04b: nodes go OFFLINE from boot).
  // Returns the number of nodes that became OFFLINE in this call.
  uint32_t tick(uint32_t now_ms);
  // Vest compliance (FR-28): COMPLIANT only when all 5 nodes are WORN (UNKNOWN counts as not compliant).
  bool vest_compliant() const { return vest_compliant_; }
  uint8_t nodes_not_ok_mask() const;      // bit n set = node n is not WORN
  // Alert decision (D-10): any node NOT_WORN or OFFLINE. UNKNOWN never starts it. The motor FSM (S5 part 2) reads this.
  bool alert_active() const;
  // Messages generated by process(), tick(), note_boot() and note_fault() wait here until the glue takes them
  // (under the pipeline lock) and emits them to the sink after releasing the lock (serial output takes milliseconds).
  void note_boot(uint32_t reset_reason, uint32_t now_ms);     // HUB_BOOT (B-05)
  void note_fault(uint8_t fault_code, uint32_t now_ms);       // HUB_FAULT (X-01..X-07), used from S5 part 2
  uint32_t take_messages(HubMsg* out, uint32_t max);          // oldest first; returns the number copied
  uint32_t messages_pending() const { return msg_n_; }
  uint32_t messages_dropped() const { return msg_dropped_; }

  void note_scan_quiet() { g_.scan_quiet_60s++; }
  bool accept_enabled() const { return accept_enabled_; }
  bool registry_ok() const { return registry_ok_; }
  uint8_t vest_id() const { return vest_id_; }
  const HubGlobalCounters& global() const { return g_; }
  const HubNode& node(uint8_t id) const;   // id 1..5; any other id returns an all-zero slot

 private:
  uint8_t vest_id_;
  uint8_t key_[32];
  bool key_ok_;
  bool registry_ok_;
  bool accept_enabled_;
  HubGlobalCounters g_;
  HubNode node_[HUB_NODE_SLOTS + 1];   // index = node id; slot 0 unused
  HubNode dummy_;
  uint32_t heartbeat_s_, margin_s_;
  bool vest_compliant_;
  HubMsg msgq_[HUB_MSG_QUEUE_DEPTH];
  uint32_t msg_n_, msg_dropped_;
  void push_msg(const HubMsg& m);
  void eval_compliance(uint32_t now_ms);   // enqueues one COMPLIANCE on a vest-level change only (C-01..C-04)
};

const char* hub_node_state_name(uint8_t s);
const char* hub_verdict_name(HubVerdict v);
