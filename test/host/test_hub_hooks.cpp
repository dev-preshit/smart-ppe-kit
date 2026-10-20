// S3 story 5: serial test hooks (parser) and the dump format. Hooks exist only with -DPPE_TEST_HOOKS.
#include "ppe_test.h"
#include <string>
#include <vector>
#include "hub_hooks.h"
#include "hub_dump.h"
#include "fake_hub_store.h"
#include "gen/vectors.h"

PPE_TEST(hook_inject_parses_hex_with_separators, "TC-22") {
  HubHookCmd c = hub_hook_parse("inject 14FFFFFF60010301");
  CHECK_EQ(c.type, HUB_HOOK_INJECT); CHECK_EQ(c.len, 8); CHECK_EQ(c.data[0], 0x14); CHECK_EQ(c.data[7], 0x01);
  c = hub_hook_parse("  inject 14 ff:FF,ff 60\r\n");
  CHECK_EQ(c.type, HUB_HOOK_INJECT); CHECK_EQ(c.len, 5); CHECK_EQ(c.data[1], 0xFF); CHECK_EQ(c.data[4], 0x60);
  c = hub_hook_parse("inject");
  CHECK_EQ(c.type, HUB_HOOK_INJECT); CHECK_EQ(c.len, 0);                    // zero-length defect
}

PPE_TEST(hook_inject_full_a1_vector_roundtrips, "TC-23") {
  std::string hex; char b[3];
  for (int i = 0; i < 21; i++) { snprintf(b, sizeof b, "%02X", V_A1_ADV[i]); hex += b; }
  HubHookCmd c = hub_hook_parse(("inject " + hex).c_str());
  CHECK_EQ(c.type, HUB_HOOK_INJECT); CHECK_EQ(c.len, 21); CHECK_MEM(c.data, V_A1_ADV, 21);
}

PPE_TEST(hook_inject_rejects_bad_input, "TC-22") {
  CHECK_EQ(hub_hook_parse("inject 1").type, HUB_HOOK_ERROR);                 // odd digit count
  CHECK_EQ(hub_hook_parse("inject 1 4").type, HUB_HOOK_ERROR);               // separator inside a byte
  CHECK_EQ(hub_hook_parse("inject zz").type, HUB_HOOK_ERROR);
  std::string big = "inject "; for (unsigned i = 0; i < HUB_RX_ADV_MAX + 1; i++) big += "AA";
  CHECK_EQ(hub_hook_parse(big.c_str()).type, HUB_HOOK_ERROR);                // longer than the queue entry
  std::string max = "inject "; for (unsigned i = 0; i < HUB_RX_ADV_MAX; i++) max += "AA";
  CHECK_EQ(hub_hook_parse(max.c_str()).type, HUB_HOOK_INJECT);
}

PPE_TEST(hook_stall_time_rxlog_dump_commands, "TC-21") {
  CHECK_EQ(hub_hook_parse("stall 1").type, HUB_HOOK_STALL); CHECK_EQ(hub_hook_parse("stall 1").n, 1);
  CHECK_EQ(hub_hook_parse("stall 3").n, 3);
  CHECK_EQ(hub_hook_parse("stall").type, HUB_HOOK_ERROR);
  CHECK_EQ(hub_hook_parse("stall 0").type, HUB_HOOK_ERROR);
  CHECK_EQ(hub_hook_parse("stall 10").type, HUB_HOOK_ERROR);
  CHECK_EQ(hub_hook_parse("dump").type, HUB_HOOK_DUMP);
  CHECK_EQ(hub_hook_parse("dump now").type, HUB_HOOK_ERROR);
  CHECK_EQ(hub_hook_parse("time").type, HUB_HOOK_TIME_NOW);
  CHECK_EQ(hub_hook_parse("time on").type, HUB_HOOK_TIME_ON);
  CHECK_EQ(hub_hook_parse("time off").type, HUB_HOOK_TIME_OFF);
  CHECK_EQ(hub_hook_parse("rxlog on").type, HUB_HOOK_RXLOG_ON);
  CHECK_EQ(hub_hook_parse("rxlog off").type, HUB_HOOK_RXLOG_OFF);
  CHECK_EQ(hub_hook_parse("rxlog").type, HUB_HOOK_ERROR);                    // argument required, default is off
  CHECK_EQ(hub_hook_parse("").type, HUB_HOOK_NONE);
  CHECK_EQ(hub_hook_parse("   \r\n").type, HUB_HOOK_NONE);
  CHECK_EQ(hub_hook_parse("reboot").type, HUB_HOOK_ERROR);
  CHECK_EQ(hub_hook_parse("injectx 00").type, HUB_HOOK_ERROR);
}

static void collect(void* ctx, const char* line) { static_cast<std::vector<std::string>*>(ctx)->push_back(line); }

PPE_TEST(dump_lists_registry_counters_queue_and_the_callbacks_identity, "TC-32") {
  FakeHubStore st; st.has[3] = true; st.val[3] = 100;
  HubPipeline h; h.init(1, V_KEY); h.load_registry(&st, 7);
  h.process(V_A1_ADV, 21, true, 500);                    // accepted (258 > 100): gap 157
  h.process(V_A1_ADV, 21, true, 600);                    // duplicate
  h.process(V_A1_ADV, 20, true, 700);                    // drop_len
  HubDumpExtra x; memset(&x, 0, sizeof x); x.now_ms = 1234; x.queue_full = 2; x.queue_fill = 1; x.queue_depth = 64;
  std::vector<std::string> out; hub_dump_emit(h, x, collect, &out);
  std::string all; for (size_t i = 0; i < out.size(); i++) all += out[i] + "\n";
  CHECK(all.find("DUMP begin t_ms=1234") != std::string::npos);
  CHECK(all.find("rx_total=3 drop_len=1 drop_ver=0 drop_vest=0 drop_node=0 drop_state=0 scan_quiet_60s=0") != std::string::npos);
  CHECK(all.find("callbacks_seen=5 (=rx_total+queue_full)") != std::string::npos);   // 3 + 2
  CHECK(all.find("DUMP node=3 state=WORN last_counter=258 last_seen_ms=500 accepted=1 dup=1 replay=0 gap=157 hmac_fail=0") != std::string::npos);
  CHECK(all.find("DUMP node=1 state=UNKNOWN last_counter=0 last_seen_ms=7") != std::string::npos);
  CHECK(out.front() == "DUMP begin t_ms=1234"); CHECK(out.back() == "DUMP end");
  CHECK_EQ(out.size(), 13);                              // begin, hub, global, queue, 5 nodes, persist, proc, scan, end
  for (size_t i = 0; i < out.size(); i++) CHECK(out[i].compare(0, 5, "DUMP ") == 0 && out[i].size() < 200);
}

// A copy of the pipeline (taken by the dump hook under the lock, printed after it) must print the same dump.
PPE_TEST(dump_of_a_copy_equals_dump_of_the_original, "TC-32") {
  FakeHubStore st; HubPipeline h; h.init(1, V_KEY); h.load_registry(&st, 0);
  h.process(V_A1_ADV, 21, true, 500);
  h.process(V_A1_ADV, 21, true, 600);
  HubDumpExtra x; memset(&x, 0, sizeof x); x.now_ms = 1000; x.queue_depth = 64;
  std::vector<std::string> a, b;
  hub_dump_emit(h, x, collect, &a);
  HubPipeline snap; snap = h;
  h.process(V_A1_ADV, 21, true, 700);             // the original moves on after the copy
  hub_dump_emit(snap, x, collect, &b);
  CHECK_EQ(a.size(), b.size());
  for (size_t i = 0; i < a.size() && i < b.size(); i++) CHECK(a[i] == b[i]);
}
