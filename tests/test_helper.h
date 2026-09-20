#pragma once
#include <cstdio>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                      \
  do {                                                                   \
    ++g_checks;                                                          \
    if (!(cond)) {                                                       \
      ++g_failures;                                                      \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
    }                                                                    \
  } while (0)

static int test_summary() {
  if (g_failures > 0) {
    std::printf("FAILED: %d/%d checks\n", g_failures, g_checks);
    return 1;
  }
  std::printf("OK: %d checks passed\n", g_checks);
  return 0;
}
