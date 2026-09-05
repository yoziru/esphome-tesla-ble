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
    CHECK(decision.refresh == ControlStateRefresh::NONE);
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

static void test_successful_commands_publish_and_refresh_authoritative_state() {
  const struct {
    ControlStateCommand command;
    ControlStateRefresh refresh;
  } cases[] = {
      {ControlStateCommand::CHARGING_STATE, ControlStateRefresh::CHARGE_STATE},
      {ControlStateCommand::SET_AMPS, ControlStateRefresh::CHARGE_STATE},
      {ControlStateCommand::SET_LIMIT, ControlStateRefresh::CHARGE_STATE},
      {ControlStateCommand::SET_STEERING_WHEEL_HEAT, ControlStateRefresh::CLIMATE_STATE},
      {ControlStateCommand::SET_SENTRY_MODE, ControlStateRefresh::CLOSURES_STATE},
  };

  for (const auto &test : cases) {
    const auto decision = control_state_decision(test.command, true);
    CHECK(decision.publish_requested_state);
    CHECK(!decision.republish_confirmed_number_state);
    CHECK(decision.refresh == test.refresh);
  }
}

int main() {
  test_failed_commands_do_not_change_control_state();
  test_failed_number_commands_republish_the_last_confirmed_state();
  test_successful_commands_publish_and_refresh_authoritative_state();

  return test_summary();
}
