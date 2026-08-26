// Behavioural tests for ConnectionResetPolicy (components/tesla_ble_vehicle/
// connection_reset_policy.h). No ESPHome or tesla-ble dependency - builds with
// a plain C++ compiler:  make test-cpp  (or just  make test).
//
// The policy detects a wedged BLE link: the GATT connection is ESTABLISHED but
// the TeslaBLE Vehicle reports disconnected (e.g. after the library's
// auth-stuck watchdog reset connection state while the link stayed up). In
// that state nothing else cycles the link - only a fresh connect cycle re-runs
// service discovery / notify registration, which restores Vehicle
// connectivity.
//
// Scenarios covered:
//  - normal operation and normal (re)connect sequences must never force a
//    disconnect
//  - a genuinely wedged link forces exactly one reconnect after a grace
//    period, then backs off before trying again
//  - connectivity blips restart the grace window

#include <cstdio>

#include "connection_reset_policy.h"

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

static void test_healthy_link_never_triggers() {
  ConnectionResetPolicy p;
  for (uint32_t t = 0; t <= 300000; t += 5000) {
    CHECK(!p.should_force_reconnect(t, true, true));
  }
}

static void test_no_gatt_link_never_triggers() {
  ConnectionResetPolicy p;
  // Nothing to fix while there is no GATT connection at all - ble_client owns
  // scanning/connecting in that state.
  for (uint32_t t = 0; t <= 300000; t += 5000) {
    CHECK(!p.should_force_reconnect(t, false, false));
  }
}

static void test_normal_connect_sequence_does_not_trigger() {
  ConnectionResetPolicy p;
  // Realistic bring-up: physical link established first, service discovery +
  // notify registration follow, only then does Vehicle report connected. The
  // temporary mismatch must ride out the grace window.
  CHECK(!p.should_force_reconnect(0, false, false));
  CHECK(!p.should_force_reconnect(2000, true, false));   // ESTABLISHED, discovery running
  CHECK(!p.should_force_reconnect(8000, true, false));   // still registering notifications
  CHECK(!p.should_force_reconnect(9000, true, true));    // fully up

  // ...and stays quiet afterwards.
  for (uint32_t t = 10000; t <= 120000; t += 5000) {
    CHECK(!p.should_force_reconnect(t, true, true));
  }
}

static void test_slow_but_successful_setup_does_not_trigger() {
  ConnectionResetPolicy p;
  // A slow car/radio that takes most of the grace window is still fine as
  // long as connectivity lands before it expires.
  CHECK(!p.should_force_reconnect(10000, true, false));
  CHECK(!p.should_force_reconnect(25000, true, false));
  CHECK(!p.should_force_reconnect(28000, true, true));
}

static void test_wedged_link_triggers_once_then_backs_off() {
  ConnectionResetPolicy p;
  // Wedge begins at t=1000 and never heals on its own.
  CHECK(!p.should_force_reconnect(1000, true, false));

  uint32_t fired_at = 0;
  for (uint32_t t = 6000; t <= 120000; t += 2000) {
    if (p.should_force_reconnect(t, true, false)) {
      fired_at = t;
      break;
    }
    CHECK(t < 60000);  // must fire within about a minute, not hang forever
  }
  CHECK(fired_at != 0);
  p.on_force_reconnect(fired_at);

  // After forcing, stay quiet for a substantial cool-off even though the
  // mismatch persists (the forced cycle needs time to play out)...
  for (uint32_t t = fired_at + 2000; t < fired_at + 45000; t += 2000) {
    CHECK(!p.should_force_reconnect(t, true, false));
  }

  // ...but if the wedge survives the retry, we do try again eventually.
  bool refired = false;
  for (uint32_t t = fired_at + 45000; t <= fired_at + 300000; t += 2000) {
    if (p.should_force_reconnect(t, true, false)) {
      refired = true;
      break;
    }
  }
  CHECK(refired);
}

static void test_connectivity_blip_restarts_the_window() {
  ConnectionResetPolicy p;
  // Mismatch starts...
  CHECK(!p.should_force_reconnect(1000, true, false));
  // ...connectivity briefly restores mid-grace...
  CHECK(!p.should_force_reconnect(8000, true, true));
  // ...then the wedge returns. The observation window restarts: it should not
  // fire immediately just because the earlier mismatch had already run a while.
  CHECK(!p.should_force_reconnect(9000, true, false));
  CHECK(!p.should_force_reconnect(15000, true, false));

  uint32_t fired_at = 0;
  for (uint32_t t = 16000; t <= 90000; t += 1000) {
    if (p.should_force_reconnect(t, true, false)) {
      fired_at = t;
      break;
    }
  }
  CHECK(fired_at != 0);
}

static void test_stalled_setup_triggers_after_grace() {
  // A link stuck in CONNECTED (service discovery or notify registration
  // failed - the component maps this into "link up but not ready") must be
  // force-cycled once it outlives the grace window, instead of sitting
  // half-initialised forever.
  ConnectionResetPolicy p;
  CHECK(!p.should_force_reconnect(1000, true, false));
  CHECK(!p.should_force_reconnect(10000, true, false));
  CHECK(p.should_force_reconnect(25000, true, false));
}

int main() {
  test_healthy_link_never_triggers();
  test_no_gatt_link_never_triggers();
  test_normal_connect_sequence_does_not_trigger();
  test_slow_but_successful_setup_does_not_trigger();
  test_wedged_link_triggers_once_then_backs_off();
  test_connectivity_blip_restarts_the_window();
  test_stalled_setup_triggers_after_grace();

  if (g_failures > 0) {
    std::printf("FAILED: %d/%d checks\n", g_failures, g_checks);
    return 1;
  }
  std::printf("OK: %d checks passed\n", g_checks);
  return 0;
}
