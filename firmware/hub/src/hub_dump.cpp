#include "hub_dump.h"
#ifdef PPE_TEST_HOOKS
#include <stdio.h>

#define UL(x) ((unsigned long)(x))

void hub_dump_emit(const HubPipeline& h, const HubDumpExtra& x, HubDumpEmit emit, void* ctx) {
  char b[200];
  const HubGlobalCounters& g = h.global();
  snprintf(b, sizeof b, "DUMP begin t_ms=%lu", UL(x.now_ms)); emit(ctx, b);
  snprintf(b, sizeof b, "DUMP hub vest=0x%02X accept_enabled=%d registry_ok=%d", h.vest_id(), h.accept_enabled(), h.registry_ok());
  emit(ctx, b);
  snprintf(b, sizeof b, "DUMP global rx_total=%lu drop_len=%lu drop_ver=%lu drop_vest=%lu drop_node=%lu drop_state=%lu scan_quiet_60s=%lu",
           UL(g.rx_total), UL(g.drop_len), UL(g.drop_ver), UL(g.drop_vest), UL(g.drop_node), UL(g.drop_state), UL(g.scan_quiet_60s));
  emit(ctx, b);
  snprintf(b, sizeof b, "DUMP queue fill=%lu depth=%lu max_fill=%lu queue_full=%lu callbacks_seen=%lu (=rx_total+queue_full)",
           UL(x.queue_fill), UL(x.queue_depth), UL(x.queue_max_fill), UL(x.queue_full), UL(g.rx_total + x.queue_full));
  emit(ctx, b);
  for (uint8_t i = 1; i <= HUB_NODE_SLOTS; i++) {
    const HubNode& n = h.node(i);
    snprintf(b, sizeof b, "DUMP node=%u state=%s last_counter=%lu last_seen_ms=%lu accepted=%lu dup=%lu replay=%lu gap=%lu hmac_fail=%lu battery=%u flags=0x%02X",
             i, hub_node_state_name(n.state), UL(n.last_counter), UL(n.last_seen_ms), UL(n.c.accepted), UL(n.c.dup_count),
             UL(n.c.replay_count), UL(n.c.gap_count), UL(n.c.hmac_fail), n.battery, n.flags);
    emit(ctx, b);
  }
  snprintf(b, sizeof b, "DUMP persist pending=%lu writes=%lu write_fails=%lu dropped=%lu last_us=%lu max_us=%lu avg_us=%lu",
           UL(x.persist_pending), UL(x.persist_writes), UL(x.persist_write_fails), UL(x.persist_dropped),
           UL(x.persist_last_us), UL(x.persist_max_us), UL(x.persist_avg_us));
  emit(ctx, b);
  snprintf(b, sizeof b, "DUMP proc count=%lu last_us=%lu max_us=%lu avg_us=%lu over_10ms=%lu",
           UL(x.proc_count), UL(x.proc_last_us), UL(x.proc_max_us), UL(x.proc_avg_us), UL(x.proc_over_10ms));
  emit(ctx, b);
  snprintf(b, sizeof b, "DUMP scan restarts=%lu consecutive_fails=%lu", UL(x.scan_restarts), UL(x.scan_consecutive_fails));
  emit(ctx, b);
  emit(ctx, "DUMP end");
}
#endif
