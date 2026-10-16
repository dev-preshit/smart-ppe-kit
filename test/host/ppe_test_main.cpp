#include "ppe_test.h"
#include <stdlib.h>
int g_ppe_fail_count = 0;
long g_ppe_check_count = 0;
PpeTestCase*& ppe_test_head() { static PpeTestCase* h = nullptr; return h; }

int main(int argc, char** argv) {
  // collect in registration order (list is built reversed)
  PpeTestCase* arr[512]; int n = 0;
  for (PpeTestCase* c = ppe_test_head(); c && n < 512; c = c->next) arr[n++] = c;
  bool list = argc > 1 && strcmp(argv[1], "--list") == 0;
  int failed = 0, run = 0;
  for (int i = n - 1; i >= 0; i--) {
    if (list) { printf("%s\t%s\n", arr[i]->tc, arr[i]->name); continue; }
    g_ppe_fail_count = 0; long c0 = g_ppe_check_count;
    arr[i]->fn(); run++;
    printf("TEST\t%s\t%s\t%s\tchecks=%ld\n", arr[i]->tc, arr[i]->name, g_ppe_fail_count ? "FAIL" : "PASS", g_ppe_check_count - c0);
    if (g_ppe_fail_count) failed++;
  }
  if (!list) printf("SUMMARY\ttests=%d\tfailed=%d\tchecks=%ld\n", run, failed, g_ppe_check_count);
  return failed ? 1 : 0;
}
