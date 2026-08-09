// Behavioural tests for state_text (components/tesla_ble_vehicle/state_text.h).
// No ESPHome or tesla-ble dependency - builds with a plain C++ compiler:
//   make test-cpp  (or just  make test)
//
// These tests describe desired behaviour of the raw state -> text/flag
// conversions that feed the sensors: what each VCSEC/CarServer state maps to.

#include <cstdio>

#include "state_text.h"

using namespace esphome::tesla_ble_vehicle::state_text;

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

#define CHECK_OPT(opt, expected) CHECK((opt).has_value() && (opt).value() == (expected))
#define CHECK_STR(actual, expected) CHECK((actual) == (expected))

static void test_sleep_status() {
  CHECK_OPT(sleep_status(kSleepAwake), false);
  CHECK_OPT(sleep_status(kSleepAsleep), true);
  CHECK(!sleep_status(kSleepUnknown).has_value());
  CHECK(!sleep_status(99).has_value());
}

static void test_lock_status() {
  CHECK_OPT(lock_status(kLockUnlocked), true);
  CHECK_OPT(lock_status(kLockSelectiveUnlocked), true);
  CHECK_OPT(lock_status(kLockLocked), false);
  CHECK_OPT(lock_status(kLockInternalLocked), false);
  CHECK(!lock_status(99).has_value());
}

static void test_user_presence() {
  CHECK_OPT(user_presence(kPresencePresent), true);
  CHECK_OPT(user_presence(kPresenceNotPresent), false);
  CHECK(!user_presence(kPresenceUnknown).has_value());
  CHECK(!user_presence(99).has_value());
}

static void test_is_charging() {
  CHECK(is_charging(kChargingStateCharging));
  CHECK(is_charging(kChargingStateStarting));
  CHECK(!is_charging(kChargingStateComplete));
  CHECK(!is_charging(kChargingStateStopped));
  CHECK(!is_charging(kChargingStateDisconnected));
  CHECK(!is_charging(kChargingStateNoPower));
  CHECK(!is_charging(kChargingStateUnknown));
  CHECK(!is_charging(99));
}

static void test_charging_state_text() {
  CHECK_STR(charging_state(kChargingStateDisconnected), "Disconnected");
  CHECK_STR(charging_state(kChargingStateNoPower), "No Power");
  CHECK_STR(charging_state(kChargingStateStarting), "Starting");
  CHECK_STR(charging_state(kChargingStateCharging), "Charging");
  CHECK_STR(charging_state(kChargingStateComplete), "Complete");
  CHECK_STR(charging_state(kChargingStateStopped), "Stopped");
  CHECK_STR(charging_state(kChargingStateCalibrating), "Calibrating");
  CHECK_STR(charging_state(kChargingStateUnknown), "Unknown");
  CHECK_STR(charging_state(99), "Unknown");
}

static void test_charger_connected() {
  CHECK(!charger_connected(kChargingStateDisconnected));
  CHECK(!charger_connected(kChargingStateUnknown));
  CHECK(charger_connected(kChargingStateNoPower));
  CHECK(charger_connected(kChargingStateStarting));
  CHECK(charger_connected(kChargingStateCharging));
  CHECK(charger_connected(kChargingStateComplete));
  CHECK(charger_connected(kChargingStateStopped));
  CHECK(charger_connected(kChargingStateCalibrating));
}

static void test_iec61851_state_text() {
  CHECK_STR(iec61851_state(kChargingStateDisconnected), "A");
  CHECK_STR(iec61851_state(kChargingStateNoPower), "E");
  CHECK_STR(iec61851_state(kChargingStateStarting), "C");
  CHECK_STR(iec61851_state(kChargingStateCharging), "C");
  CHECK_STR(iec61851_state(kChargingStateCalibrating), "C");
  CHECK_STR(iec61851_state(kChargingStateComplete), "B");
  CHECK_STR(iec61851_state(kChargingStateStopped), "B");
  CHECK_STR(iec61851_state(kChargingStateUnknown), "F");
  CHECK_STR(iec61851_state(99), "F");
}

static void test_shift_state_text() {
  CHECK_STR(shift_state(kShiftP), "P");
  CHECK_STR(shift_state(kShiftR), "R");
  CHECK_STR(shift_state(kShiftN), "N");
  CHECK_STR(shift_state(kShiftD), "D");
  CHECK_STR(shift_state(kShiftSNA), "SNA");
  CHECK_STR(shift_state(kShiftInvalid), "Invalid");
  CHECK_STR(shift_state(99), "Unknown");
}

static void test_is_parked() {
  CHECK(is_parked(kShiftP));
  CHECK(!is_parked(kShiftR));
  CHECK(!is_parked(kShiftN));
  CHECK(!is_parked(kShiftD));
  CHECK(!is_parked(kShiftInvalid));
}

static void test_charge_limit_reason_text() {
  CHECK_STR(charge_limit_reason(kLimitNone), "None");
  CHECK_STR(charge_limit_reason(kLimitEvse), "EVSE");
  CHECK_STR(charge_limit_reason(kLimitBattTempLow), "BattTempLow");
  CHECK_STR(charge_limit_reason(kLimitHighSoc), "HighSoc");
  CHECK_STR(charge_limit_reason(kLimitCabin), "Cabin");
  CHECK_STR(charge_limit_reason(kLimitUnknown), "Unknown");
  CHECK_STR(charge_limit_reason(99), "Unknown");
}

int main() {
  test_sleep_status();
  test_lock_status();
  test_user_presence();
  test_is_charging();
  test_charging_state_text();
  test_charger_connected();
  test_iec61851_state_text();
  test_shift_state_text();
  test_is_parked();
  test_charge_limit_reason_text();

  if (g_failures > 0) {
    std::printf("FAILED: %d/%d checks\n", g_failures, g_checks);
    return 1;
  }
  std::printf("OK: %d checks passed\n", g_checks);
  return 0;
}
