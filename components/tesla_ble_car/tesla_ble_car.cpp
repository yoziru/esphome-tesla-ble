#include "tesla_ble_car.h"
#include "client.h"
#include "esphome/core/log.h"
#include "utils.h"

#include <nvs_flash.h>

namespace esphome
{
  namespace tesla_ble_car
  {

    static const char *const TAG = "tesla_ble_car";
    TeslaBLECar::TeslaBLECar()
        : m_pClient(new TeslaBLE::Client{})
    {
      ESP_LOGI(TAG, "Starting");

      esp_err_t err = nvs_flash_init();
      if (err != ESP_OK)
      {
        ESP_LOGE(TAG, "Failed to initialize flash: %s", esp_err_to_name(err));
        esp_restart();
      }

      err = nvs_open("storage", NVS_READWRITE, &this->storage_handle_);
      if (err != ESP_OK)
      {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
        esp_restart();
      }
      size_t required_private_key_size = 0;
      err = nvs_get_blob(this->storage_handle_, "private_key", NULL,
                         &required_private_key_size);

      if (err != ESP_OK)
      {
        ESP_LOGE(TAG, "Failed read private key from storage: %s",
                 esp_err_to_name(err));
      }

      if (required_private_key_size == 0)
      {
        int result_code = m_pClient->CreatePrivateKey();
        if (result_code != 0)
        {
          ESP_LOGI(TAG, "Failed create private key");
          esp_restart();
        }

        unsigned char private_key_buffer[300];
        size_t private_key_length = 0;
        m_pClient->GetPrivateKey(private_key_buffer, sizeof(private_key_buffer),
                                 &private_key_length);

        esp_err_t err = nvs_set_blob(this->storage_handle_, "private_key",
                                     private_key_buffer, private_key_length);

        err = nvs_commit(this->storage_handle_);
        if (err != ESP_OK)
        {
          ESP_LOGE(TAG, "Failed commit storage: %s", esp_err_to_name(err));
        }

        ESP_LOGI(TAG, "Private key successfully created");
      }
      else
      {
        unsigned char private_key_buffer[required_private_key_size];
        err = nvs_get_blob(this->storage_handle_, "private_key", private_key_buffer,
                           &required_private_key_size);
        if (err != ESP_OK)
        {
          ESP_LOGE(TAG, "Failed read private key from storage: %s",
                   esp_err_to_name(err));
          esp_restart();
        }

        int result_code =
            m_pClient->LoadPrivateKey(private_key_buffer, required_private_key_size);
        if (result_code != 0)
        {
          ESP_LOGE(TAG, "Failed load private key");
          esp_restart();
        }

        ESP_LOGI(TAG, "Private key loaded successfully");
        TeslaBLE::DumpBuffer("\n", private_key_buffer, required_private_key_size);
      }

      size_t required_tesla_key_size = 0;
      err = nvs_get_blob(this->storage_handle_, "tesla_key", NULL, &required_tesla_key_size);
      if (required_tesla_key_size > 0)
      {
        // isWhitelisted = true;
        unsigned char tesla_key_buffer[required_tesla_key_size];

        err = nvs_get_blob(this->storage_handle_, "tesla_key", tesla_key_buffer,
                           &required_tesla_key_size);
        if (err != ESP_OK)
        {
          ESP_LOGE(TAG, "Failed read tesla key from storage: %s",
                   esp_err_to_name(err));
          esp_restart();
        }

        int result_code =
            m_pClient->LoadTeslaKey(tesla_key_buffer, required_tesla_key_size);

        if (result_code != 0)
        {
          ESP_LOGE(TAG, "Failed load tesla key");
          esp_restart();
        }

        ESP_LOGI(TAG, "Tesla key loaded successfully");
        this->isAuthenticated = true;
      }

      uint32_t counter = 0;
      err = nvs_get_u32(this->storage_handle_, "counter", &counter);
      if (err != ESP_OK)
      {
        ESP_LOGE(TAG, "Failed read counter from storage: %s", esp_err_to_name(err));
      }

      if (counter > 0)
      {
        m_pClient->SetCounter(&counter);
        ESP_LOGI(TAG, "Loaded old counter %lu", counter);
      }
      // load_key(m_pClient);
      // m_pClient->CreatePrivateKey();

      this->service_uuid_ = espbt::ESPBTUUID::from_raw(SERVICE_UUID);
      this->read_uuid_ = espbt::ESPBTUUID::from_raw(READ_UUID);
      this->write_uuid_ = espbt::ESPBTUUID::from_raw(WRITE_UUID);
      this->isAuthenticated = false;
    }

    void TeslaBLECar::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                          esp_ble_gattc_cb_param_t *param)
    {
      switch (event)
      {
      case ESP_GATTC_OPEN_EVT:
      {
        if (param->open.status == ESP_GATT_OK)
        {
          ESP_LOGI(TAG, "Connected successfully!");

          auto *writeCharacteristic = this->parent()->get_characteristic(this->service_uuid_, this->write_uuid_);
          if (writeCharacteristic == nullptr)
          {
            ESP_LOGW(TAG, "No write characteristic found at service %s write %s",
                     this->service_uuid_.to_string().c_str(),
                     this->write_uuid_.to_string().c_str());
            break;
          }
          this->write_handle_ = writeCharacteristic->handle;

          if (isAuthenticated == false)
          {
            unsigned char whitelist_message_buffer[200];
            size_t whitelist_message_length = 0;
            int return_code = m_pClient->BuildWhiteListMessage(
                whitelist_message_buffer, &whitelist_message_length);

            if (return_code != 0)
            {
              ESP_LOGE(TAG, "Failed to build whitelist message");
              break;
            }

            auto write_status =
                esp_ble_gattc_write_char_descr(this->parent()->get_gattc_if(), this->parent()->get_conn_id(), this->write_handle_,
                                               whitelist_message_length, whitelist_message_buffer, ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
            if (write_status)
            {
              ESP_LOGW(TAG, "Error sending CCC descriptor write request, status=%d", write_status);
              break;
            }
            ESP_LOGI(TAG, "Please tap your card on the reader now..");

            // if (writeCharacteristic->writeValue(whitelist_message_buffer,
            //                                     whitelist_message_length)) {
            //   ESP_LOGI(TAG, "Please tap your card on the reader now..");
            // }
          }
        }
        break;
      }

      case ESP_GATTC_DISCONNECT_EVT:
      {
        this->handle_ = 0;
        this->read_handle_ = 0;
        this->write_handle_ = 0;
        ESP_LOGW(TAG, "Disconnected!");
        break;
      }

      case ESP_GATTC_SEARCH_CMPL_EVT:
      {
        // if (this->request_read_values_()) {
        //   if (!this->read_battery_next_update_) {
        //     this->node_state = espbt::ClientState::ESTABLISHED;
        //   } else {
        //     // delay setting node_state to ESTABLISHED until confirmation of the notify registration
        //     this->request_battery_();
        //   }
        // }

        // ensure that the client will be disconnected even if no responses arrive
        this->set_response_timeout_();

        break;
      }

      case ESP_GATTC_READ_CHAR_EVT:
      {
        if (param->read.conn_id != this->parent()->get_conn_id())
          break;
        if (param->read.status != ESP_GATT_OK)
        {
          ESP_LOGW(TAG, "Error reading char at handle %d, status=%d", param->read.handle, param->read.status);
          break;
        }
        // if (param->read.handle == this->handle_) {
        //   this->read_sensors(param->read.value, param->read.value_len);
        // }
        break;
      }

      case ESP_GATTC_REG_FOR_NOTIFY_EVT:
      {
        this->node_state = espbt::ClientState::ESTABLISHED;
        break;
      }

      case ESP_GATTC_NOTIFY_EVT:
      {
        if (param->notify.conn_id != this->parent()->get_conn_id())
          break;
        // if (param->notify.handle == this->read_handle_) {
        //   this->read_battery_(param->notify.value, param->notify.value_len);
        // }

        ESP_LOGD(TAG, "ESP_GATTC_NOTIFY_EVT, value_len=%d", param->notify.value_len);
        VCSEC_FromVCSECMessage message_o = VCSEC_FromVCSECMessage_init_zero;
        int return_code = m_pClient->ParseFromVCSECMessage(param->notify.value, param->notify.value_len, &message_o);
        if (return_code != 0)
        {
          ESP_LOGE(TAG, "Failed to parse incoming message\n");
          return;
        }
        break;
      }

      default:
        break;
      }
    }

    void TeslaBLECar::update()
    {
      if (this->node_state != espbt::ClientState::ESTABLISHED)
      {
        if (!this->parent()->enabled)
        {
          ESP_LOGW(TAG, "Reconnecting to device");
          this->parent()->set_enabled(true);
          this->parent()->connect();
        }
        else
        {
          ESP_LOGW(TAG, "Connection in progress");
        }
      }
    }

    void TeslaBLECar::test()
    {
      unsigned char private_key_buffer[300];
      size_t private_key_length = 0;
      m_pClient->GetPrivateKey(private_key_buffer, sizeof(private_key_buffer), &private_key_length);
      ESP_LOGD(TAG, "Private key: %s\n", private_key_buffer);

      unsigned char message_buffer[200];
      size_t message_buffer_length = 0;
      m_pClient->BuildWhiteListMessage(message_buffer, &message_buffer_length);

      auto chr_status = esp_ble_gattc_write_char(this->parent()->get_gattc_if(), this->parent()->get_conn_id(), this->write_handle_, message_buffer_length, message_buffer, ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
      if (chr_status)
      {
        ESP_LOGW(TAG, "Error sending the white list message, status=%d", chr_status);
        return;
      }
    }

    void TeslaBLECar::response_pending_()
    {
      this->responses_pending_++;
      this->set_response_timeout_();
    }

    void TeslaBLECar::response_received_()
    {
      if (--this->responses_pending_ == 0)
      {
        // This instance must not stay connected
        // so other clients can connect to it (e.g. the
        // mobile app).
        this->parent()->set_enabled(false);
      }
    }

    void TeslaBLECar::set_response_timeout_()
    {
      this->set_timeout("response_timeout", 5 * 1000, [this]()
                        {
    this->responses_pending_ = 1;
    this->response_received_(); });
    }

  } // namespace tesla_ble_car
} // namespace esphome
