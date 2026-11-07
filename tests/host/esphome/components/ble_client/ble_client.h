#pragma once
#include <esp_gattc_api.h>
#include <esphome/components/esp32_ble_tracker/esp32_ble_tracker.h>
namespace esphome::ble_client {
struct BLEClient {
  struct Characteristic { uint16_t handle = 1; } characteristic;
  Characteristic *get_characteristic(esp32_ble_tracker::ESPBTUUID, esp32_ble_tracker::ESPBTUUID) { return &characteristic; }
  int get_gattc_if() { return 0; }
  const uint8_t *get_remote_bda() { return nullptr; }
  uint16_t get_conn_id() { return 0; }
  void disconnect() {}
};
class BLEClientNode {
 public:
  virtual void gattc_event_handler(esp_gattc_cb_event_t, esp_gatt_if_t, esp_ble_gattc_cb_param_t *) {}
  esp32_ble_tracker::ClientState node_state = esp32_ble_tracker::ClientState::IDLE;
  BLEClient *parent() { return &client_; }
 private:
  BLEClient client_;
};
}
