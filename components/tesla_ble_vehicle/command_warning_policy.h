#pragma once

namespace esphome {
namespace tesla_ble_vehicle {

enum class CommandWarningAction { CLEAR, MOMENTARY };

// A command failure is an event; a healthy completion or an intentional skip
// means a previous command warning no longer represents current state.
inline CommandWarningAction command_warning_action(bool is_success, bool is_skipped) {
  return is_success || is_skipped ? CommandWarningAction::CLEAR : CommandWarningAction::MOMENTARY;
}

inline constexpr char COMMAND_WARNING_TIMEOUT[] = "command-failed-warning";

}  // namespace tesla_ble_vehicle
}  // namespace esphome
