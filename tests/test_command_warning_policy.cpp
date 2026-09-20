#include <cassert>

#include "command_warning_policy.h"

using namespace esphome::tesla_ble_vehicle;

struct FakeWarningStatus {
  bool warning_active = false;
  bool timeout_armed = false;
  bool persistent_warning = false;

  void status_momentary_warning(const char *) {
    warning_active = true;
    persistent_warning = false;
    timeout_armed = true;
  }

  void cancel_timeout(const char *) { timeout_armed = false; }
  void status_clear_warning() {
    warning_active = false;
    persistent_warning = false;
  }

  void status_set_warning(const char *) {
    warning_active = true;
    persistent_warning = true;
  }

  void run_timeout() {
    if (timeout_armed) {
      timeout_armed = false;
      warning_active = false;
      persistent_warning = false;
    }
  }
};

static void test_already_set_command_warning_expires() {
  FakeWarningStatus status;
  const auto cancel_timeout = [&status]() {
    status.cancel_timeout(COMMAND_WARNING_TIMEOUT);
  };

  // Tesla may report Set Charging Limit as already_set even though BLE is healthy.
  apply_command_warning(status, CommandOutcome::FAILED, cancel_timeout);
  assert(status.warning_active);
  assert(status.timeout_armed);
  status.run_timeout();
  assert(!status.warning_active);
}

static void test_healthy_command_results_clear_a_previous_warning() {
  FakeWarningStatus status;
  const auto cancel_timeout = [&status]() {
    status.cancel_timeout(COMMAND_WARNING_TIMEOUT);
  };

  apply_command_warning(status, CommandOutcome::FAILED, cancel_timeout);
  apply_command_warning(status, CommandOutcome::SUCCESS, cancel_timeout);
  assert(!status.warning_active);
  assert(!status.timeout_armed);

  apply_command_warning(status, CommandOutcome::FAILED, cancel_timeout);
  apply_command_warning(status, CommandOutcome::SKIPPED, cancel_timeout);
  assert(!status.warning_active);
  assert(!status.timeout_armed);
}

static void test_connection_loss_preserves_persistent_warning() {
  FakeWarningStatus status;
  const auto cancel_timeout = [&status]() {
    status.cancel_timeout(COMMAND_WARNING_TIMEOUT);
  };

  apply_command_warning(status, CommandOutcome::FAILED, cancel_timeout);
  cancel_command_warning(cancel_timeout);
  status.status_set_warning("BLE connection lost");
  status.run_timeout();
  assert(status.warning_active);
  assert(status.persistent_warning);
}

int main() {
  test_already_set_command_warning_expires();
  test_healthy_command_results_clear_a_previous_warning();
  test_connection_loss_preserves_persistent_warning();
}
