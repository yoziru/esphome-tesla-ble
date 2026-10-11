// Behavioural tests for seat_climate_policy.h: option index -> command
// mapping and reported state -> option index.
//
// No ESPHome or tesla-ble dependency - builds with a plain C++ compiler:
//   make test-cpp  (or just  make test)

#include "test_helper.h"

#include "seat_climate_policy.h"

using namespace esphome::tesla_ble_vehicle;

static void test_option_index_maps_to_the_requested_direction() {
  CHECK(seat_climate_selection(0, true, true).valid);
  CHECK(seat_climate_selection(0, true, true).level == 0);

  const auto low_heat = seat_climate_selection(1, true, true);
  CHECK(low_heat.valid);
  CHECK(!low_heat.cooling);
  CHECK(!low_heat.auto_mode);
  CHECK(low_heat.level == 1);

  const auto high_heat = seat_climate_selection(3, true, true);
  CHECK(high_heat.valid && high_heat.level == 3 && !high_heat.cooling);

  const auto low_cool = seat_climate_selection(4, true, true);
  CHECK(low_cool.valid && low_cool.cooling && low_cool.level == 1);

  const auto high_cool = seat_climate_selection(6, true, true);
  CHECK(high_cool.valid && high_cool.cooling && high_cool.level == 3);

  const auto auto_mode = seat_climate_selection(7, true, true);
  CHECK(auto_mode.valid && auto_mode.auto_mode && !auto_mode.cooling);
}

static void test_unsupported_options_are_rejected() {
  // A heat-only seat has no cooling or Auto options.
  CHECK(!seat_climate_selection(4, false, false).valid);
  CHECK(!seat_climate_selection(7, false, false).valid);
  CHECK(seat_climate_selection(3, false, false).valid);
  // A seat without Auto rejects only the Auto option.
  CHECK(!seat_climate_selection(7, true, false).valid);
  CHECK(seat_climate_selection(6, true, false).valid);
  // Out-of-range indices never map to a command.
  CHECK(!seat_climate_selection(8, true, true).valid);
  CHECK(!seat_climate_selection(99, true, true).valid);
  CHECK(seat_climate_selection(4, false, true).auto_mode);
  CHECK(!seat_climate_selection(7, false, true).valid);
  CHECK(seat_climate_option_index(0, -1, true, false, true) == 4);
}

static void test_reported_state_selects_the_matching_option() {
  CHECK(seat_climate_option_index(0, 0, false, true, true) == 0);
  CHECK(seat_climate_option_index(1, 0, false, true, true) == 1);
  CHECK(seat_climate_option_index(3, 0, false, true, true) == 3);
  CHECK(seat_climate_option_index(0, 2, false, true, true) == 5);
  CHECK(seat_climate_option_index(0, 0, true, true, true) == 7);
  // Auto outranks a stale level the car still reports.
  CHECK(seat_climate_option_index(2, 0, true, true, true) == 7);
  // A heat-only seat ignores a reported cooling level.
  CHECK(seat_climate_option_index(0, 0, false, false, false) == 0);
  CHECK(seat_climate_option_index(2, 0, false, false, false) == 2);
  // Nothing reported: no option is published.
  CHECK(seat_climate_option_index(-1, -1, false, true, true) == -1);
  CHECK(seat_climate_option_index(0, -1, false, true, true) == 0);
}

int main() {
  test_option_index_maps_to_the_requested_direction();
  test_unsupported_options_are_rejected();
  test_reported_state_selects_the_matching_option();

  return test_summary();
}
