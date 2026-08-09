// Behavioural tests for InfotainmentPollPolicy (components/tesla_ble_vehicle/
// polling_policy.h). No ESPHome or tesla-ble dependency - builds with a plain
// C++ compiler:  make test-cpp  (or just  make test).
//
// These tests describe desired behaviour, not implementation details:
//  - idle cars (including unlocked / user-present) must be allowed to sleep
//  - only charging and sentry mode keep the car awake
//  - natural awake blips must not restart the aggressive polling window
//  - genuine activity after sleep resumes fast, wake-enabled polling

#include <cstdio>

#include "polling_policy.h"

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

// Defaults used across tests: 30s awake, 10s active, 11 min sleep timeout.
static void configure(InfotainmentPollPolicy &p) {
  p.set_awake_interval_ms(30000);
  p.set_active_interval_ms(10000);
  p.set_sleep_timeout_ms(660000);
}

static void test_default_intervals() {
  InfotainmentPollPolicy p;
  CHECK(p.update(1000, false, false, false).interval_ms == 30000);
  CHECK(p.update(1000, false, true, false).interval_ms == 10000);
  CHECK(p.update(1000, true, false, false).interval_ms == 660000);
}

static void test_idle_polling_backs_off_after_timeout() {
  InfotainmentPollPolicy p;
  configure(p);

  // Fresh connection: an idle car polls gently with WAKE_IF_NEEDED...
  InfotainmentPollDecision d = p.update(1000, false, false, false);
  CHECK(d.wake_policy == WakePolicy::WAKE_IF_NEEDED);
  CHECK(d.interval_ms == 30000);

  // ...for the whole idle window...
  d = p.update(1000 + 600000, false, false, false);
  CHECK(d.wake_policy == WakePolicy::WAKE_IF_NEEDED);

  // ...then backs off to NO_WAKE_SKIP so it can fall asleep (#202).
  d = p.update(1000 + 660000, false, false, false);
  CHECK(d.wake_policy == WakePolicy::NO_WAKE_SKIP);
  CHECK(d.interval_ms == 660000);
}

static void test_observed_asleep_skips_wake_polling() {
  InfotainmentPollPolicy p;
  configure(p);

  // Car already asleep: never wake it, poll slowly.
  InfotainmentPollDecision d = p.update(1000, true, false, false);
  CHECK(d.wake_policy == WakePolicy::NO_WAKE_SKIP);
  CHECK(d.interval_ms == 660000);
}

static void test_charging_keeps_vehicle_awake() {
  InfotainmentPollPolicy p;
  configure(p);

  InfotainmentPollDecision d = p.update(1000, false, true, false);
  CHECK(d.wake_policy == WakePolicy::WAKE_IF_NEEDED);
  CHECK(d.interval_ms == 10000);

  // Hours later (charging takes a while) still fully awake.
  d = p.update(1000 + 7200000, false, true, false);
  CHECK(d.wake_policy == WakePolicy::WAKE_IF_NEEDED);
  CHECK(d.interval_ms == 10000);
}

static void test_sentry_mode_keeps_vehicle_awake() {
  InfotainmentPollPolicy p;
  configure(p);

  InfotainmentPollDecision d = p.update(1000, false, false, true);
  CHECK(d.wake_policy == WakePolicy::WAKE_IF_NEEDED);
  CHECK(d.interval_ms == 10000);

  d = p.update(1000 + 7200000, false, false, true);
  CHECK(d.wake_policy == WakePolicy::WAKE_IF_NEEDED);
  CHECK(d.interval_ms == 10000);
}

static void test_unlocked_or_presence_does_not_keep_awake() {
  InfotainmentPollPolicy p;
  configure(p);

  // Left unlocked (or user present because a phone is in range) is not an
  // active state: the car must still be allowed to fall asleep (#202).
  p.update(1000, false, false, false);
  InfotainmentPollDecision d = p.update(1000 + 660000, false, false, false);
  CHECK(d.wake_policy == WakePolicy::NO_WAKE_SKIP);
}

static void test_awake_blip_does_not_resume_wake_polling() {
  InfotainmentPollPolicy p;
  configure(p);

  p.update(1000, false, false, false);
  CHECK(p.update(1000 + 660000, false, false, false).wake_policy == WakePolicy::NO_WAKE_SKIP);

  // The car blips awake on its own, then sleeps again. This must NOT restart
  // the aggressive polling window or it would be re-woken every time (#201).
  CHECK(p.update(1000 + 700000, false, false, false).wake_policy == WakePolicy::NO_WAKE_SKIP);
  CHECK(p.update(1000 + 730000, true, false, false).wake_policy == WakePolicy::NO_WAKE_SKIP);
}

static void test_activity_after_sleep_resumes_fast_polling() {
  InfotainmentPollPolicy p;
  configure(p);

  p.update(1000, false, false, false);
  CHECK(p.update(1000 + 660000, false, false, false).wake_policy == WakePolicy::NO_WAKE_SKIP);

  // Charging starts: immediately back to fast, wake-enabled polling.
  InfotainmentPollDecision d = p.update(1000 + 700000, false, true, false);
  CHECK(d.wake_policy == WakePolicy::WAKE_IF_NEEDED);
  CHECK(d.interval_ms == 10000);
}

static void test_reset_starts_fresh_idle_window() {
  InfotainmentPollPolicy p;
  configure(p);

  p.update(1000, false, false, false);
  // Reconnect: forget how long the car has been idle.
  p.reset();
  InfotainmentPollDecision d = p.update(1000000, false, false, false);
  CHECK(d.wake_policy == WakePolicy::WAKE_IF_NEEDED);
  CHECK(d.interval_ms == 30000);
}

static void test_idle_timeout_counts_from_charging_stop() {
  InfotainmentPollPolicy p;
  configure(p);

  // Charging for 2 hours, then charging completes.
  p.update(1000, false, true, false);
  p.update(1000 + 7200000, false, true, false);
  p.update(1000 + 7200000 + 1, false, false, false);

  // The idle window runs from when charging stopped, not from connect:
  // shortly after the stop we still poll with WAKE_IF_NEEDED...
  InfotainmentPollDecision d = p.update(1000 + 7200000 + 100000, false, false, false);
  CHECK(d.wake_policy == WakePolicy::WAKE_IF_NEEDED);
  CHECK(d.interval_ms == 30000);

  // ...and only after the full timeout from the stop do we back off.
  d = p.update(1000 + 7200000 + 660000, false, false, false);
  CHECK(d.wake_policy == WakePolicy::NO_WAKE_SKIP);
}

static void test_idle_timeout_counts_from_sentry_off() {
  InfotainmentPollPolicy p;
  configure(p);

  // Sentry was on overnight, then turned off.
  p.update(1000, false, false, true);
  p.update(1000 + 28800000, false, false, true);
  p.update(1000 + 28800000 + 1, false, false, false);

  InfotainmentPollDecision d = p.update(1000 + 28800000 + 100000, false, false, false);
  CHECK(d.wake_policy == WakePolicy::WAKE_IF_NEEDED);

  d = p.update(1000 + 28800000 + 660000, false, false, false);
  CHECK(d.wake_policy == WakePolicy::NO_WAKE_SKIP);
}

static void test_charging_and_sentry_together_stay_awake() {
  InfotainmentPollPolicy p;
  configure(p);

  InfotainmentPollDecision d = p.update(1000, false, true, true);
  CHECK(d.wake_policy == WakePolicy::WAKE_IF_NEEDED);
  CHECK(d.interval_ms == 10000);
}

// --- poll scheduling (should_poll / on_poll) ---

static void test_poll_due_when_no_poll_fired_yet() {
  InfotainmentPollPolicy p;
  configure(p);
  // last_poll_ms_ starts 0 and millis() is long past the interval after boot:
  // the first tick is immediately due.
  CHECK(p.should_poll(50000, 30000));
  p.on_poll(50000);
  CHECK(!p.should_poll(50000 + 10000, 30000));
}

static void test_poll_due_after_interval() {
  InfotainmentPollPolicy p;
  configure(p);
  p.on_poll(1000);
  CHECK(!p.should_poll(1000 + 29999, 30000));
  CHECK(p.should_poll(1000 + 30000, 30000));
}

static void test_poll_tracks_fire_time() {
  InfotainmentPollPolicy p;
  configure(p);
  p.on_poll(1000);
  // fires again on the next cadence
  CHECK(p.should_poll(1000 + 30000, 30000));
  p.on_poll(1000 + 30000);
  CHECK(!p.should_poll(1000 + 30000 + 1000, 30000));
}

static void test_force_update_throttled_to_active_interval() {
  InfotainmentPollPolicy p;
  configure(p);
  p.on_poll(1000);
  // force_update is throttled to the active interval
  CHECK(!p.should_poll(1000 + 5000, p.active_interval_ms()));
  CHECK(p.should_poll(1000 + 10000, p.active_interval_ms()));
}

static void test_poll_stays_correct_across_millis_wraparound() {
  InfotainmentPollPolicy p;
  configure(p);
  // Last poll 10s before the ~49 day millis() wraparound.
  const uint32_t last_poll = 0xFFFFFFFFu - 10000u;
  p.on_poll(last_poll);
  // After the wrap, elapsed time still counts correctly.
  CHECK(!p.should_poll(5000, 30000));      // elapsed 15s < 30s
  CHECK(p.should_poll(30000, 30000));      // elapsed 40s >= 30s
}

static void test_full_tick_cadence_while_charging() {
  InfotainmentPollPolicy p;
  configure(p);

  // t=20000: charging; nothing fired yet, so the first poll is due immediately.
  InfotainmentPollDecision d = p.update(20000, false, true, false);
  CHECK(d.interval_ms == 10000);
  CHECK(p.should_poll(20000, d.interval_ms));
  p.on_poll(20000);

  // t=20000+5000: within the 10s active interval, no poll.
  d = p.update(20000 + 5000, false, true, false);
  CHECK(d.interval_ms == 10000);
  CHECK(!p.should_poll(20000 + 5000, d.interval_ms));

  // t=20000+10000: due again.
  d = p.update(20000 + 10000, false, true, false);
  CHECK(p.should_poll(20000 + 10000, d.interval_ms));
}

int main() {
  test_default_intervals();
  test_idle_polling_backs_off_after_timeout();
  test_observed_asleep_skips_wake_polling();
  test_charging_keeps_vehicle_awake();
  test_sentry_mode_keeps_vehicle_awake();
  test_unlocked_or_presence_does_not_keep_awake();
  test_awake_blip_does_not_resume_wake_polling();
  test_activity_after_sleep_resumes_fast_polling();
  test_idle_timeout_counts_from_charging_stop();
  test_idle_timeout_counts_from_sentry_off();
  test_charging_and_sentry_together_stay_awake();
  test_reset_starts_fresh_idle_window();
  test_poll_due_when_no_poll_fired_yet();
  test_poll_due_after_interval();
  test_poll_tracks_fire_time();
  test_force_update_throttled_to_active_interval();
  test_poll_stays_correct_across_millis_wraparound();
  test_full_tick_cadence_while_charging();

  if (g_failures > 0) {
    std::printf("FAILED: %d/%d checks\n", g_failures, g_checks);
    return 1;
  }
  std::printf("OK: %d checks passed\n", g_checks);
  return 0;
}
