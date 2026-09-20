#pragma once

namespace esphome {
namespace tesla_ble_vehicle {

enum class CommandOutcome { SUCCESS, SKIPPED, FAILED };

inline constexpr char COMMAND_WARNING_TIMEOUT[] = "command-failed-warning";

// A command failure is an event; a healthy completion or an intentional skip
// means a previous command warning no longer represents current state.
template <typename WarningSink>
void apply_command_warning(WarningSink &sink, CommandOutcome outcome) {
  if (outcome == CommandOutcome::FAILED) {
    sink.status_momentary_warning(COMMAND_WARNING_TIMEOUT);
    return;
  }

  sink.cancel_timeout(COMMAND_WARNING_TIMEOUT);
  sink.status_clear_warning();
}

template <typename WarningSink>
void cancel_command_warning(WarningSink &sink) {
  sink.cancel_timeout(COMMAND_WARNING_TIMEOUT);
}

}  // namespace tesla_ble_vehicle
}  // namespace esphome
