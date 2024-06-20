#include "tesla_ble_listener.h"
#include "esphome/core/log.h"
#include <cinttypes>

#ifdef USE_ESP32

namespace esphome {
namespace tesla_ble_listener {

static const char *const TAG = "tesla_ble_listener";

bool TeslaBLEListener::parse_device(const esp32_ble_tracker::ESPBTDevice &device) {
  for (auto &it : device.get_manufacturer_datas()) {
    // manufacturer ID can match multiple manufacturers
    if (it.uuid == esp32_ble_tracker::ESPBTUUID::from_uint32(0x004C)) {
      if (it.data.size() < 4)
        continue;

      uint32_t sn = it.data[0];
      sn |= ((uint32_t) it.data[1] << 8);
      sn |= ((uint32_t) it.data[2] << 16);
      sn |= ((uint32_t) it.data[3] << 24);

      // only log if device name is not empty
      if (not device.get_name().empty()) {
        // device name should match sha1 of VIN
        ESP_LOGD(TAG, "Found possible Tesla device Serial: %" PRIu32 " | Name: %s | MAC: %s", sn, device.get_name().c_str(), device.address_str().c_str());
      }
    }
  }

  return false;
}

}  // namespace tesla_ble_listener
}  // namespace esphome

#endif
