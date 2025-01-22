#pragma once

#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#include "esphome/core/component.h"

namespace esphome {
namespace tesla_ble_listener {

std::string get_vin_advertisement_name(const char *vin);

class TeslaBLEListener : public esp32_ble_tracker::ESPBTDeviceListener {
public:
  std::string vin_ad_name_;

  bool parse_device(const esp32_ble_tracker::ESPBTDevice &device) override;
  void set_vin(const char *vin) {
    vin_ad_name_ = get_vin_advertisement_name(vin);
  }
};

} // namespace tesla_ble_listener
} // namespace esphome
