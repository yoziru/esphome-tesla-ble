// Behavioural tests for control_state_policy.h (post-command UI decisions).
// State *refreshes* are scheduled by the TeslaBLE library itself now, so the
// policy only decides about optimistic publishes and number republishing.
//
// No ESPHome or tesla-ble dependency - builds with a plain C++ compiler:
//   make test-cpp  (or just  make test)

#include "test_helper.h"

#include <initializer_list>

#include "control_state_policy.h"

using namespace esphome::tesla_ble_vehicle;

static void test_failed_commands_do_not_change_control_state() {
  for (const auto command : {ControlStateCommand::CHARGING_STATE,
                             ControlStateCommand::SET_AMPS,
                             ControlStateCommand::SET_LIMIT,
                             ControlStateCommand::SET_STEERING_WHEEL_HEAT,
                             ControlStateCommand::SET_SENTRY_MODE}) {
    const auto decision = control_state_decision(command, false);
    CHECK(!decision.publish_requested_state);
  }
}

static void test_failed_number_commands_republish_the_last_confirmed_state() {
  CHECK(!control_state_decision(ControlStateCommand::CHARGING_STATE, false)
             .republish_confirmed_number_state);
  CHECK(control_state_decision(ControlStateCommand::SET_AMPS, false)
            .republish_confirmed_number_state);
  CHECK(control_state_decision(ControlStateCommand::SET_LIMIT, false)
            .republish_confirmed_number_state);
}

static void test_successful_commands_publish_the_requested_state() {
  for (const auto command : {ControlStateCommand::CHARGING_STATE,
                             ControlStateCommand::SET_AMPS,
                             ControlStateCommand::SET_LIMIT,
                             ControlStateCommand::SET_STEERING_WHEEL_HEAT,
                             ControlStateCommand::SET_SENTRY_MODE}) {
    const auto decision = control_state_decision(command, true);
    CHECK(decision.publish_requested_state);
    CHECK(!decision.republish_confirmed_number_state);
  }
}

int main() {
  test_failed_commands_do_not_change_control_state();
  test_failed_number_commands_republish_the_last_confirmed_state();
  test_successful_commands_publish_the_requested_state();

  return test_summary();
}
