#pragma once

namespace esphome {
namespace tesla_ble_vehicle {

enum class ControlStateCommand {
  CHARGING_STATE,
  SET_AMPS,
  SET_LIMIT,
  SET_STEERING_WHEEL_HEAT,
  SET_SENTRY_MODE,
};

enum class ControlStateRefresh { NONE, CHARGE_STATE, CLIMATE_STATE, CLOSURES_STATE };

struct ControlStateDecision {
  bool publish_requested_state;
  bool republish_confirmed_number_state;
  ControlStateRefresh refresh;
};

inline ControlStateDecision control_state_decision(ControlStateCommand command,
                                                    bool command_succeeded) {
  if (!command_succeeded) {
    const bool is_number_command = command == ControlStateCommand::SET_AMPS ||
                                   command == ControlStateCommand::SET_LIMIT;
    return {false, is_number_command, ControlStateRefresh::NONE};
  }

  switch (command) {
    case ControlStateCommand::CHARGING_STATE:
    case ControlStateCommand::SET_AMPS:
    case ControlStateCommand::SET_LIMIT:
      return {true, false, ControlStateRefresh::CHARGE_STATE};
    case ControlStateCommand::SET_STEERING_WHEEL_HEAT:
      return {true, false, ControlStateRefresh::CLIMATE_STATE};
    case ControlStateCommand::SET_SENTRY_MODE:
      return {true, false, ControlStateRefresh::CLOSURES_STATE};
  }

  return {false, false, ControlStateRefresh::NONE};
}

}  // namespace tesla_ble_vehicle
}  // namespace esphome
