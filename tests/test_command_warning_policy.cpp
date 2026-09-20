#include <cassert>

#include "command_warning_policy.h"

using namespace esphome::tesla_ble_vehicle;

int main() {
  assert(command_warning_action(false, false) == CommandWarningAction::MOMENTARY);
  assert(command_warning_action(true, false) == CommandWarningAction::CLEAR);
  assert(command_warning_action(false, true) == CommandWarningAction::CLEAR);
}
