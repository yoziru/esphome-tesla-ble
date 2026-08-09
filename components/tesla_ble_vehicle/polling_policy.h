#pragma once

#include <cstdint>

namespace esphome {
namespace tesla_ble_vehicle {

// Whether infotainment polling is allowed to wake the vehicle. A vehicle that
// is observed asleep, or has been idle past the sleep timeout, must be polled
// with NO_WAKE_SKIP so it can stay asleep.
enum class WakePolicy : uint8_t { WAKE_IF_NEEDED, NO_WAKE_SKIP };

struct InfotainmentPollDecision {
  uint32_t interval_ms;
  WakePolicy wake_policy;
};

// Decides the infotainment polling cadence and wake policy from the current
// vehicle state. Pure logic with no ESPHome or tesla-ble dependency so it can
// be unit-tested in isolation (see tests/test_polling_policy.cpp).
//
// Behaviour:
//  - Active charging or sentry mode keep the car awake: fast interval with
//    WAKE_IF_NEEDED, and the idle window keeps being refreshed.
//  - Otherwise the car is treated as idle: it is polled gently at the awake
//    interval, and after sleep_timeout_ms of idle time it backs off to
//    NO_WAKE_SKIP so the car can fall asleep on its own.
//  - Only genuine activity resets the idle timer. A car that is observed
//    asleep, or briefly blips awake on its own, must not restart the aggressive
//    window or it would be re-woken every time it tried to sleep (#201/#202).
class InfotainmentPollPolicy {
public:
  void set_awake_interval_ms(uint32_t interval_ms) { awake_interval_ms_ = interval_ms; }
  void set_active_interval_ms(uint32_t interval_ms) { active_interval_ms_ = interval_ms; }
  void set_sleep_timeout_ms(uint32_t interval_ms) { sleep_timeout_ms_ = interval_ms; }

  uint32_t awake_interval_ms() const { return awake_interval_ms_; }
  uint32_t active_interval_ms() const { return active_interval_ms_; }
  uint32_t sleep_timeout_ms() const { return sleep_timeout_ms_; }

  // Forget the idle timing, e.g. on (re)connect. The next update() starts a
  // fresh idle window.
  void reset() { idle_since_ms_ = 0; }

  InfotainmentPollDecision update(uint32_t now_ms, bool is_asleep, bool is_charging, bool is_sentry_mode) {
    const bool active = is_charging || is_sentry_mode;
    if (active) {
      idle_since_ms_ = now_ms;
    } else if (idle_since_ms_ == 0) {
      idle_since_ms_ = now_ms;
    }
    const bool effective_asleep =
        is_asleep || (!active && (now_ms - idle_since_ms_ >= sleep_timeout_ms_));

    uint32_t interval_ms = awake_interval_ms_;
    if (effective_asleep) {
      interval_ms = sleep_timeout_ms_;
    } else if (active) {
      interval_ms = active_interval_ms_;
    }
    return {interval_ms,
            effective_asleep ? WakePolicy::NO_WAKE_SKIP : WakePolicy::WAKE_IF_NEEDED};
  }

private:
  uint32_t awake_interval_ms_{30000};
  uint32_t active_interval_ms_{10000};
  uint32_t sleep_timeout_ms_{660000};
  uint32_t idle_since_ms_{0};
};

}  // namespace tesla_ble_vehicle
}  // namespace esphome
