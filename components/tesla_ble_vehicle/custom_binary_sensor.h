#pragma once

#include "esphome/components/binary_sensor/binary_sensor.h"

// https://github.com/esphome/feature-requests/issues/2324
namespace esphome {
namespace binary_sensor {

class CustomBinarySensor : public BinarySensor {
public:
  void set_has_state(bool has_state) {
    this->has_state_ = has_state;
    this->state_callback_.call(this->state);
  }
};

} // namespace binary_sensor
} // namespace esphome
