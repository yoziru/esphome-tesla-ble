#include "tesla_ble_listener.h"
#include "esphome/core/log.h"
#include <string>
#include <vin_utils.h>

namespace esphome {
namespace tesla_ble_listener {
static const char *const TAG = "tesla_ble_listener";

std::string get_vin_advertisement_name(const char *vin) {
  std::string result = TeslaBLE::get_vin_advertisement_name(vin);
  ESP_LOGD(TAG, "VIN advertisement name: %s", result.c_str());
  return result;
}

bool TeslaBLEListener::parse_device(
    const esp32_ble_tracker::ESPBTDevice &device) {
  char addr_buf[MAC_ADDRESS_PRETTY_BUFFER_SIZE];
  const char *addr_str = device.address_str_to(addr_buf);
  ESP_LOGD(TAG, "Parsing device: [%s]: %s", addr_str,
           device.get_name().c_str());

  if (device.get_name() == this->vin_ad_name_) {
    ESP_LOGI(TAG, "Found Tesla vehicle | Name: %s | MAC: %s",
             device.get_name().c_str(), addr_str);
    return true;
  }

  return false;
}
} // namespace tesla_ble_listener
} // namespace esphome
