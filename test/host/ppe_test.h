// Minimal dependency-free test harness. Each test is tagged with the test-plan TC it supports.
#pragma once
#include <stdio.h>
#include <stdint.h>
#include <string.h>

struct PpeTestCase { const char* name; const char* tc; void (*fn)(); PpeTestCase* next; };
PpeTestCase*& ppe_test_head();
struct PpeTestReg { PpeTestReg(PpeTestCase* c) { c->next = ppe_test_head(); ppe_test_head() = c; } };
extern int g_ppe_fail_count;   // failures in the current test
extern long g_ppe_check_count;

#define PPE_TEST(name, tc) \
  static void test_##name(); \
  static PpeTestCase case_##name = {#name, tc, test_##name, nullptr}; \
  static PpeTestReg reg_##name(&case_##name); \
  static void test_##name()

#define CHECK(cond) do { g_ppe_check_count++; if (!(cond)) { g_ppe_fail_count++; \
  fprintf(stderr, "  CHECK FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); g_ppe_check_count++; \
  if (_a != _b) { g_ppe_fail_count++; fprintf(stderr, "  CHECK_EQ FAILED %s:%d: %s (%lld) != %s (%lld)\n", \
  __FILE__, __LINE__, #a, _a, #b, _b); } } while (0)
#define CHECK_MEM(a, b, n) CHECK(memcmp((a), (b), (n)) == 0)
