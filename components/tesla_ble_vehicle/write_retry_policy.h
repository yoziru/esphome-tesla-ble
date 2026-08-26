#pragma once

#include <cstdint>

namespace esphome {
namespace tesla_ble_vehicle {

enum class WriteAttemptDecision { ATTEMPT, WAIT, DROP };

// Retry discipline for BLE GATT writes. GATT writes fail transiently
// (ESP_GATT_BUSY, status 133 while a link is sick) and persistently (stale
// conn_id after a link flap). Retrying the head chunk every loop() iteration
// floods the log and blocks all newer traffic forever when the failure is
// permanent.
//
// Pure logic with no ESPHome or tesla-ble dependency so it can be unit-tested
// in isolation (see tests/test_write_retry_policy.cpp).
//
// Behaviour:
//  - an untried chunk is attempted immediately
//  - after a failure the chunk waits out a growing backoff (100ms doubling,
//    capped) before the next attempt
//  - a success forgets all failure history
//  - after MAX_CONSECUTIVE_FAILURES the chunk is declared poison: DROP it so
//    newer traffic proceeds (the vehicle ignores the truncated message; a
//    wedged queue is strictly worse)
class WriteRetryPolicy {
 public:
  static constexpr uint32_t MAX_CONSECUTIVE_FAILURES = 8;
  static constexpr uint32_t INITIAL_BACKOFF_MS = 100;
  static constexpr uint32_t MAX_BACKOFF_MS = 2000;

  // What to do with the head chunk at now_ms. Call every loop() iteration.
  // Unsigned arithmetic stays correct across the ~49 day millis() wraparound.
  WriteAttemptDecision next_action(uint32_t now_ms) const {
    if (failure_count_ == 0) {
      return WriteAttemptDecision::ATTEMPT;
    }
    if (failure_count_ >= MAX_CONSECUTIVE_FAILURES) {
      return WriteAttemptDecision::DROP;
    }
    return now_ms - last_failure_ms_ >= current_backoff_ms_ ? WriteAttemptDecision::ATTEMPT
                                                            : WriteAttemptDecision::WAIT;
  }

  // Record that an attempt just failed at now_ms.
  void on_failure(uint32_t now_ms) {
    if (failure_count_ == 0) {
      current_backoff_ms_ = INITIAL_BACKOFF_MS;
    } else {
      const uint32_t doubled = current_backoff_ms_ * 2;
      current_backoff_ms_ = doubled > MAX_BACKOFF_MS ? MAX_BACKOFF_MS : doubled;
    }
    ++failure_count_;
    last_failure_ms_ = now_ms;
  }

  // Record that a write succeeded: history is forgotten.
  void on_success(uint32_t /*now_ms*/) {
    failure_count_ = 0;
    current_backoff_ms_ = INITIAL_BACKOFF_MS;
  }

  // Record that the head chunk was dropped (after next_action returned DROP):
  // rearm for the following chunk.
  void on_drop() {
    failure_count_ = 0;
    current_backoff_ms_ = INITIAL_BACKOFF_MS;
  }

  // Forget all state, e.g. on reconnect / queue teardown.
  void reset() {
    failure_count_ = 0;
    current_backoff_ms_ = INITIAL_BACKOFF_MS;
    last_failure_ms_ = 0;
  }

 private:
  uint32_t failure_count_{0};
  uint32_t current_backoff_ms_{INITIAL_BACKOFF_MS};
  uint32_t last_failure_ms_{0};
};

}  // namespace tesla_ble_vehicle
}  // namespace esphome
