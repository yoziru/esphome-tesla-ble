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

struct ControlStateDecision {
  bool publish_requested_state;
  bool republish_confirmed_number_state;
};

// Post-command UI decisions: whether to publish the requested state right away
// (the vehicle confirms it moments later) and whether a number entity must be
// republished so the HA slider snaps back when the command failed.
// State *refreshes* after commands are scheduled by the TeslaBLE library itself.
inline ControlStateDecision control_state_decision(ControlStateCommand command, bool command_succeeded) {
  if (!command_succeeded) {
    const bool is_number_command =
        command == ControlStateCommand::SET_AMPS || command == ControlStateCommand::SET_LIMIT;
    return {false, is_number_command};
  }

  switch (command) {
    case ControlStateCommand::CHARGING_STATE:
    case ControlStateCommand::SET_AMPS:
    case ControlStateCommand::SET_LIMIT:
    case ControlStateCommand::SET_STEERING_WHEEL_HEAT:
    case ControlStateCommand::SET_SENTRY_MODE:
      return {true, false};
  }

  return {false, false};
}

}  // namespace tesla_ble_vehicle
}  // namespace esphome
