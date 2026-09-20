#pragma once

namespace esphome {
namespace tesla_ble_vehicle {

enum class CommandOutcome { SUCCESS, SKIPPED, FAILED };

inline constexpr char COMMAND_WARNING_TIMEOUT[] = "command-failed-warning";

// A command failure is an event; a healthy completion or an intentional skip
// means a previous command warning no longer represents current state.
template <typename WarningSink, typename CancelTimeout>
void apply_command_warning(WarningSink &sink, CommandOutcome outcome,
                           CancelTimeout cancel_timeout) {
  if (outcome == CommandOutcome::FAILED) {
    sink.status_momentary_warning(COMMAND_WARNING_TIMEOUT);
    return;
  }

  cancel_timeout();
  sink.status_clear_warning();
}

template <typename CancelTimeout>
void cancel_command_warning(CancelTimeout cancel_timeout) {
  cancel_timeout();
}

}  // namespace tesla_ble_vehicle
}  // namespace esphome
