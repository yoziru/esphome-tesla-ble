#pragma once

#include <cstddef>

namespace esphome {
namespace tesla_ble_vehicle {

// One seat has one climate control, matching the vehicle UI: the same three
// levels are heating or cooling, never both, plus Auto on the front seats.
//
// This maps the select's option index to the requested action and back. The
// canonical option order below is shared by the SELECTS definitions in
// __init__.py (guarded by tests/test_codegen.py) and the control() handlers, so
// all of them stay index-compatible. Commands use the shared library's
// TeslaBLE::Vehicle::set_seat_climate().
//
//   0 Off | 1-3 Low/Medium/High heat | 4-6 Low/Medium/High cool | 7 Auto
constexpr size_t SEAT_OPTION_OFF = 0;
constexpr size_t SEAT_OPTION_HEAT_LOW = 1;
constexpr size_t SEAT_OPTION_COOL_LOW = 4;
constexpr size_t SEAT_OPTION_AUTO = 7;
constexpr int SEAT_MAX_LEVEL = 3;

struct SeatClimateSelection {
  bool valid{false};
  int level{0};  // 0-3, meaningful for heat and cool selections
  bool cooling{false};
  bool auto_mode{false};
};

// Option index chosen by the user -> the requested action.
inline SeatClimateSelection seat_climate_selection(size_t index, bool cooling_supported, bool auto_supported) {
  SeatClimateSelection selection;
  if (index == SEAT_OPTION_OFF) {
    selection.valid = true;
  } else if (index >= SEAT_OPTION_HEAT_LOW && index < SEAT_OPTION_HEAT_LOW + SEAT_MAX_LEVEL) {
    selection.valid = true;
    selection.level = static_cast<int>(index - SEAT_OPTION_HEAT_LOW) + 1;
  } else if (cooling_supported && index >= SEAT_OPTION_COOL_LOW && index < SEAT_OPTION_COOL_LOW + SEAT_MAX_LEVEL) {
    selection.valid = true;
    selection.level = static_cast<int>(index - SEAT_OPTION_COOL_LOW) + 1;
    selection.cooling = true;
  } else if (auto_supported && index == (cooling_supported ? SEAT_OPTION_AUTO : SEAT_OPTION_COOL_LOW)) {
    selection.valid = true;
    selection.auto_mode = true;
  }
  return selection;
}

// Reported vehicle state -> option index, or -1 when nothing was reported.
inline int seat_climate_option_index(int heat_level, int cool_level, bool auto_mode, bool cooling_supported,
                                     bool auto_supported) {
  if (auto_supported && auto_mode) {
    return static_cast<int>(cooling_supported ? SEAT_OPTION_AUTO : SEAT_OPTION_COOL_LOW);
  }
  if (heat_level >= 1 && heat_level <= SEAT_MAX_LEVEL) {
    return static_cast<int>(SEAT_OPTION_HEAT_LOW) + heat_level - 1;
  }
  if (cooling_supported && cool_level >= 1 && cool_level <= SEAT_MAX_LEVEL) {
    return static_cast<int>(SEAT_OPTION_COOL_LOW) + cool_level - 1;
  }
  if (heat_level == 0 || (cooling_supported && cool_level == 0)) {
    return static_cast<int>(SEAT_OPTION_OFF);
  }
  return -1;
}

}  // namespace tesla_ble_vehicle
}  // namespace esphome
