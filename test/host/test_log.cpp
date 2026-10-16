#include "ppe_test.h"
#include "ppe_log.h"
static char g_last[256]; static int g_n;
static void cap(const char* l) { strncpy(g_last, l, sizeof g_last - 1); g_n++; }

PPE_TEST(log_macro_levels_and_sink, "TC-12") {   // support code only; no dedicated TC (see docs/s1_test_map.md)
  g_n = 0; ppe_log_set_sink(cap);
  PPE_LOGI("core", "counter=%u", 7u);
  CHECK_EQ(g_n, 1); CHECK(strstr(g_last, " I core: counter=7") != nullptr);
  PPE_LOGE("core", "bad %s", "thing"); CHECK_EQ(g_n, 2); CHECK(strstr(g_last, " E core: bad thing") != nullptr);
  PPE_LOGD("core", "hidden");                     // default level 3: debug compiled out
  CHECK_EQ(g_n, 2);
  ppe_log_set_sink(nullptr);
}
