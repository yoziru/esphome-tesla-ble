#include "tesla_ble_listener.h"
#include "esphome/core/log.h"
#include <cinttypes>

#ifdef USE_ESP32

namespace esphome {
namespace tesla_ble_listener {

static const char *const TAG = "tesla_ble_listener";
static const char *const TESLA_VIN = "XP7YGCEK4NB022648";

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
      if (device.get_name() != "") {
        ESP_LOGD(TAG, "Found possible Tesla device Serial: %" PRIu32 " | Name: %s | MAC: %s", sn, device.get_name().c_str(), device.address_str().c_str());
        // device name should match sha1 of VIN (sha1 should be S7079c5022536d6bdC)
        // 
        // std::string vin = TESLA_VIN;
        // std::vector<uint8_t> vinBytes(vin.begin(), vin.end());
        // std::array<uint8_t, 20> digest;
        // sha1(vinBytes.data(), vinBytes.size(), digest.data());

        // char tesla_vin_sha1[32];
        // snprintf(tesla_vin_sha1, sizeof(tesla_vin_sha1), "S%02x%02x%02x%02x%02x%02x%02x%02x",
        //      digest[0], digest[1], digest[2], digest[3], digest[4], digest[5], digest[6], digest[7]);

        if (device.get_name() == "S7079c5022536d6bdC") {
          ESP_LOGD(TAG, "-> Device name: %s", device.get_name().c_str());
          return true;
        }
      }
    }
  }

  return false;
}

}  // namespace tesla_ble_listener
}  // namespace esphome

#endif
