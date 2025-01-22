#include "tesla_ble_listener.h"
#include "esphome/core/log.h"
#include "mbedtls/sha1.h"
#include <cinttypes>
#include <string>

namespace esphome {
namespace tesla_ble_listener {
static const char *const TAG = "tesla_ble_listener";

std::string get_vin_advertisement_name(const char *vin) {
  // * BLE advertisement local name: `S + <ID> + C`, where `<ID>` is the
  //  lower-case hex-encoding of the first eight bytes of the SHA1 digest of the
  //  Vehicle Identification Number (VIN). For example, If the VIN is
  //  `5YJS0000000000000`, then the BLE advertisement Local Name is
  //  `S1a87a5a75f3df858C`.

  unsigned char vin_sha1[20];
  size_t vin_length = 17; // Assuming standard VIN length
  int return_code =
      mbedtls_sha1((const unsigned char *)vin, vin_length, vin_sha1);
  if (return_code != 0) {
    ESP_LOGE(TAG, "Failed to calculate SHA1 of VIN");
    return "";
  }

  char result[19]; // 'S' + 16 hex chars + 'C' + null terminator
  result[0] = 'S';
  for (int i = 0; i < 8; i++) {
    snprintf(&result[1 + i * 2], 3, "%02x", vin_sha1[i]);
  }
  result[17] = 'C';
  result[18] = '\0'; // Null terminator for safe printing

  std::string result_str(result);
  ESP_LOGD(TAG, "VIN advertisement name: %s", result_str.c_str());
  return result_str;
}

bool TeslaBLEListener::parse_device(
    const esp32_ble_tracker::ESPBTDevice &device) {
  ESP_LOGD(TAG, "Parsing device: [%s]: %s", device.address_str().c_str(),
           device.get_name().c_str());

  if (device.get_name() == this->vin_ad_name_) {
    ESP_LOGI(TAG, "Found Tesla vehicle | Name: %s | MAC: %s",
             device.get_name().c_str(), device.address_str().c_str());
    return true;
  }

  return false;
}
} // namespace tesla_ble_listener
} // namespace esphome
