// Behavioural tests for WriteRetryPolicy (components/tesla_ble_vehicle/
// write_retry_policy.h). No ESPHome or tesla-ble dependency - builds with a
// plain C++ compiler:  make test-cpp  (or just  make test).
//
// BLE GATT writes fail transiently (ESP_GATT_BUSY, status 133 while a link is
// sick). The adapter previously retried the head chunk every loop() iteration
// - a warning-per-tick log storm and unbounded head-of-line blocking when the
// failure was persistent (e.g. stale conn_id).
//
// Scenarios covered:
//  - a fresh chunk is attempted immediately
//  - a failed chunk waits out a growing backoff instead of hammering
//  - a successful write forgets all failure history
//  - persistently failing chunks are dropped so newer traffic can proceed

#include <cstdio>

#include "write_retry_policy.h"

using namespace esphome::tesla_ble_vehicle;

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                      \
  do {                                                                   \
    ++g_checks;                                                          \
    if (!(cond)) {                                                       \
      ++g_failures;                                                      \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
    }                                                                    \
  } while (0)

static void test_fresh_chunk_attempts_immediately() {
  WriteRetryPolicy p;
  CHECK(p.next_action(1000) == WriteAttemptDecision::ATTEMPT);
}

static void test_failed_chunk_waits_out_backoff() {
  WriteRetryPolicy p;
  CHECK(p.next_action(1000) == WriteAttemptDecision::ATTEMPT);
  p.on_failure(1000);

  // First retry is deliberately delayed: hammering a busy link helps nobody.
  CHECK(p.next_action(1050) == WriteAttemptDecision::WAIT);
  CHECK(p.next_action(1099) == WriteAttemptDecision::WAIT);
  CHECK(p.next_action(1100) == WriteAttemptDecision::ATTEMPT);
}

static void test_backoff_grows_with_consecutive_failures() {
  WriteRetryPolicy p;
  // Fail at t=1000 (1st), retry allowed at 1100 (100ms later), fail again...
  p.on_failure(1000);
  CHECK(p.next_action(1100) == WriteAttemptDecision::ATTEMPT);
  p.on_failure(1100);
  // Second backoff must be longer than the first.
  CHECK(p.next_action(1150) == WriteAttemptDecision::WAIT);
  CHECK(p.next_action(1250) == WriteAttemptDecision::WAIT);
  CHECK(p.next_action(1300) == WriteAttemptDecision::ATTEMPT);
}

static void test_success_resets_history() {
  WriteRetryPolicy p;
  p.on_failure(1000);
  p.on_failure(1100);
  p.on_success(1200);
  // A healthy write means the link works again: next chunk goes out now.
  CHECK(p.next_action(1201) == WriteAttemptDecision::ATTEMPT);
}

static void test_persistent_failures_drop_the_chunk() {
  WriteRetryPolicy p;
  uint32_t t = 1000;
  bool dropped = false;
  // Simulate ~30s of continuous failure at the growing backoff cadence.
  for (int i = 0; i < 300 && !dropped; ++i) {
    switch (p.next_action(t)) {
      case WriteAttemptDecision::ATTEMPT:
        p.on_failure(t);
        break;
      case WriteAttemptDecision::DROP:
        dropped = true;
        break;
      case WriteAttemptDecision::WAIT:
        break;
    }
    t += 100;
  }
  CHECK(dropped);

  // After the drop the policy is rearmed for the next chunk.
  p.on_drop();
  CHECK(p.next_action(t) == WriteAttemptDecision::ATTEMPT);
}

static void test_reset_clears_everything() {
  WriteRetryPolicy p;
  p.on_failure(1000);
  p.reset();
  CHECK(p.next_action(1001) == WriteAttemptDecision::ATTEMPT);
}

int main() {
  test_fresh_chunk_attempts_immediately();
  test_failed_chunk_waits_out_backoff();
  test_backoff_grows_with_consecutive_failures();
  test_success_resets_history();
  test_persistent_failures_drop_the_chunk();
  test_reset_clears_everything();

  if (g_failures > 0) {
    std::printf("FAILED: %d/%d checks\n", g_failures, g_checks);
    return 1;
  }
  std::printf("OK: %d checks passed\n", g_checks);
  return 0;
}
