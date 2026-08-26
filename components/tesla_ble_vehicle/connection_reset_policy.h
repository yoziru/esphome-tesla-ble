#pragma once

#include <cstdint>

namespace esphome {
namespace tesla_ble_vehicle {

// Detects a wedged BLE link: the GATT connection is ESTABLISHED but the
// TeslaBLE Vehicle reports disconnected. This happens when the library's
// auth-stuck watchdog resets connection state (reset_all_sessions_and_connection_
// -> set_connected(false)) while the GATT link stays up: ble_client keeps the
// healthy-looking connection open indefinitely, and only a fresh connect
// cycle re-runs service discovery and notify registration - the only path
// that restores Vehicle connectivity (handle_connection_established).
//
// Pure logic with no ESPHome or tesla-ble dependency so it can be unit-tested
// in isolation (see tests/test_connection_reset_policy.cpp).
//
// Behaviour:
//  - a mismatch must persist for a grace window before triggering, because
//    service discovery and notify registration legitimately take a moment
//    after ESTABLISHED
//  - after a forced reconnect, re-arm only after a cooldown so a genuinely
//    sick radio/link does not churn connections
//  - restored connectivity clears mismatch tracking; a later mismatch needs
//    a fresh grace window
class ConnectionResetPolicy {
 public:
  void set_mismatch_grace_ms(uint32_t grace_ms) { mismatch_grace_ms_ = grace_ms; }
  void set_retrigger_cooldown_ms(uint32_t cooldown_ms) { retrigger_cooldown_ms_ = cooldown_ms; }

  uint32_t mismatch_grace_ms() const { return mismatch_grace_ms_; }
  uint32_t retrigger_cooldown_ms() const { return retrigger_cooldown_ms_; }

  // Forget all timing state, e.g. on (re)connect teardown.
  void reset() {
    mismatch_since_ms_ = 0;
    last_trigger_ms_ = 0;
  }

  // Whether the link should be force-cycled now. Call every loop iteration.
  // Unsigned arithmetic stays correct across the ~49 day millis() wraparound.
  bool should_force_reconnect(uint32_t now_ms, bool gatt_established, bool vehicle_connected) {
    if (!gatt_established || vehicle_connected) {
      mismatch_since_ms_ = 0;
      return false;
    }
    if (last_trigger_ms_ != 0 && now_ms - last_trigger_ms_ < retrigger_cooldown_ms_) {
      return false;
    }
    if (mismatch_since_ms_ == 0) {
      mismatch_since_ms_ = now_ms;
      return false;
    }
    return now_ms - mismatch_since_ms_ >= mismatch_grace_ms_;
  }

  // Record that a forced reconnect fired at now_ms.
  void on_force_reconnect(uint32_t now_ms) {
    last_trigger_ms_ = now_ms;
    mismatch_since_ms_ = 0;
  }

 private:
  static constexpr uint32_t DEFAULT_MISMATCH_GRACE_MS = 20000;
  static constexpr uint32_t DEFAULT_RETRIGGER_COOLDOWN_MS = 60000;

  uint32_t mismatch_grace_ms_{DEFAULT_MISMATCH_GRACE_MS};
  uint32_t retrigger_cooldown_ms_{DEFAULT_RETRIGGER_COOLDOWN_MS};
  uint32_t mismatch_since_ms_{0};
  uint32_t last_trigger_ms_{0};
};

}  // namespace tesla_ble_vehicle
}  // namespace esphome
