#include "tesla_ble_car.h"
#include "client.h"
#include "esphome/core/log.h"

namespace esphome {
namespace tesla_ble_car {

static const char *const TAG = "tesla_ble_car";
// static const BLEAddress TESLA_ADDRESS("A0:6C:65:75:13:87");

TeslaBLECar::TeslaBLECar()
    : m_pClient(new TeslaBLE::Client{})
{
    m_pClient->CreatePrivateKey();
    this->service_uuid_ = espbt::ESPBTUUID::from_raw(SERVICE_UUID);
    this->read_uuid_ = espbt::ESPBTUUID::from_raw(READ_UUID);
    this->write_uuid_ = espbt::ESPBTUUID::from_raw(WRITE_UUID);
}

void TeslaBLECar::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                            esp_ble_gattc_cb_param_t *param) {
  switch (event) {
    case ESP_GATTC_OPEN_EVT: {
      if (param->open.status == ESP_GATT_OK) {
        ESP_LOGI(TAG, "Connected successfully!");
      }
      break;
    }

    case ESP_GATTC_DISCONNECT_EVT: {
      this->handle_ = 0;
      this->acp_handle_ = 0;
      this->write_handle_ = 0;
      ESP_LOGW(TAG, "Disconnected!");
      break;
    }

    case ESP_GATTC_SEARCH_CMPL_EVT: {
      // if (this->request_read_values_()) {
      //   if (!this->read_battery_next_update_) {
      //     this->node_state = espbt::ClientState::ESTABLISHED;
      //   } else {
      //     // delay setting node_state to ESTABLISHED until confirmation of the notify registration
      //     this->request_battery_();
      //   }
      // }

      // ensure that the client will be disconnected even if no responses arrive
      // this->set_response_timeout_();

      break;
    }

    case ESP_GATTC_READ_CHAR_EVT: {
      if (param->read.conn_id != this->parent()->get_conn_id())
        break;
      if (param->read.status != ESP_GATT_OK) {
        ESP_LOGW(TAG, "Error reading char at handle %d, status=%d", param->read.handle, param->read.status);
        break;
      }
      // if (param->read.handle == this->handle_) {
      //   this->read_sensors(param->read.value, param->read.value_len);
      // }
      break;
    }

    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
      this->node_state = espbt::ClientState::ESTABLISHED;
      break;
    }

    case ESP_GATTC_NOTIFY_EVT: {
      
      if (param->notify.conn_id != this->parent()->get_conn_id())
        break;
      // if (param->notify.handle == this->acp_handle_) {
      //   this->read_battery_(param->notify.value, param->notify.value_len);
      // }

      ESP_LOGD(TAG, "ESP_GATTC_NOTIFY_EVT, value_len=%d", param->notify.value_len);
      VCSEC_FromVCSECMessage message_o = VCSEC_FromVCSECMessage_init_zero;
      int return_code = m_pClient->ParseFromVCSECMessage(param->notify.value, param->notify.value_len, &message_o);
      if (return_code != 0) {
          ESP_LOGE(TAG, "Failed to parse incoming message\n");
          return;
      }
      break;
    }

    default:
      break;
  }
}

void TeslaBLECar::update() {
  if (this->node_state != espbt::ClientState::ESTABLISHED) {
    if (!this->parent()->enabled) {
      ESP_LOGW(TAG, "Reconnecting to device");
      this->parent()->set_enabled(true);
      this->parent()->connect();
    } else {
      ESP_LOGW(TAG, "Connection in progress");
    }
  }
}

void TeslaBLECar::test() {
    unsigned char private_key_buffer[300];
    size_t private_key_length = 0;
    m_pClient->GetPrivateKey(private_key_buffer, sizeof(private_key_buffer),&private_key_length);
    ESP_LOGD(TAG, "Private key: %s\n", private_key_buffer);

    unsigned char message_buffer[200];
    size_t message_buffer_length = 0;
    m_pClient->BuildWhiteListMessage(message_buffer, &message_buffer_length);

    auto chr_status = esp_ble_gattc_write_char(this->parent()->get_gattc_if(), this->parent()->get_conn_id(), this->write_handle_, message_buffer_length, message_buffer, ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
    if (chr_status) {
        ESP_LOGW(TAG, "Error sending the white list message, status=%d", chr_status);
        return ;
    }
}

void TeslaBLECar::response_pending_() {
  this->responses_pending_++;
}

void TeslaBLECar::response_received_() {
  if (--this->responses_pending_ == 0) {
    // This instance must not stay connected
    // so other clients can connect to it (e.g. the
    // mobile app).
    this->parent()->set_enabled(false);
  }
}

}  // namespace tesla_ble_car
}  // namespace esphome
