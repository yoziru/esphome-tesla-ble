#pragma once
#include <cstdint>
using esp_gatt_if_t = int;
using esp_gatt_write_type_t = int;
using esp_gatt_auth_req_t = int;
enum esp_gattc_cb_event_t { ESP_GATTC_OPEN_EVT, ESP_GATTC_CLOSE_EVT, ESP_GATTC_DISCONNECT_EVT,
  ESP_GATTC_SEARCH_CMPL_EVT, ESP_GATTC_REG_FOR_NOTIFY_EVT, ESP_GATTC_NOTIFY_EVT, ESP_GATTC_WRITE_CHAR_EVT };
constexpr int ESP_GATT_OK = 0;
struct esp_ble_gattc_cb_param_t {
  struct { int status = 0; } open, write;
  struct { int status = 0; uint16_t handle = 0; } reg_for_notify;
  struct { uint16_t conn_id = 0, handle = 0, value_len = 0; uint8_t *value = nullptr; } notify;
};
inline int esp_ble_gattc_register_for_notify(int, const uint8_t *, uint16_t) { return 0; }
