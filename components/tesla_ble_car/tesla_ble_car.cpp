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
      ESP_LOGI(TAG, "Starting Tesla BLE Car component");

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

      this->service_uuid_ = espbt::ESPBTUUID::from_raw(SERVICE_UUID);
      this->read_uuid_ = espbt::ESPBTUUID::from_raw(READ_UUID);
      this->write_uuid_ = espbt::ESPBTUUID::from_raw(WRITE_UUID);
      this->isAuthenticated = true;
      ESP_LOGI(TAG, "Tesla BLE Car component started");
    }

    void TeslaBLECar::startPair() {
      ESP_LOGI(TAG, "Starting pairing");
      if (this->isAuthenticated == false)
      {
        unsigned char whitelist_message_buffer[200];
        size_t whitelist_message_length = 0;
        int return_code = m_pClient->BuildWhiteListMessage(
            whitelist_message_buffer, &whitelist_message_length);

        if (return_code != 0)
        {
          ESP_LOGE(TAG, "Failed to build whitelist message");
          return;
        }
        ESP_LOGV(TAG, "Whitelist message length: %d", whitelist_message_length);

        auto write_status_tap =
              esp_ble_gattc_write_char(this->parent()->get_gattc_if(), this->parent()->get_conn_id(), this->write_handle_, whitelist_message_length, whitelist_message_buffer, ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
        if (write_status_tap) {
          ESP_LOGW(TAG, "Error sending write value to BLE gattc server, status=%d", write_status_tap);
          return;
        }
        ESP_LOGI(TAG, "Please tap your card on the reader now..");
      }

      // while (this->isAuthenticated == false) {
      //   unsigned char ephemeral_key_message_buffer[200];
      //   size_t ephemeral_key_message_length = 0;
      //   int return_code = m_pClient->BuildEphemeralKeyMessage(
      //       ephemeral_key_message_buffer, &ephemeral_key_message_length);

      //   if (return_code != 0) {
      //     ESP_LOGE(TAG, "Failed to build whitelist message\n");
      //     return;
      //   }
      //   ESP_LOGD(TAG, "Ephemeral key message length: %d", ephemeral_key_message_length);


      //   auto write_status_wait =
      //       esp_ble_gattc_write_char(this->parent()->get_gattc_if(), this->parent()->get_conn_id(), this->write_handle_, ephemeral_key_message_length, ephemeral_key_message_buffer, ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
      //   if (write_status_wait) {
      //     ESP_LOGW(TAG, "Error sending write value to BLE gattc server, status=%d", write_status_wait);
      //     return;
      //   }
      //   ESP_LOGI(TAG, "Waiting for keycard to be tapped...\n");
      //   usleep(10000000);
      // }
    }

    // void TeslaBLECar::ephemeralKey() {
    //     unsigned char ephemeral_key_message_buffer[200];
    //     size_t ephemeral_key_message_length = 0;
    //     int return_code = client.BuildEphemeralKeyMessage(
    //         ephemeral_key_message_buffer, &ephemeral_key_message_length);

    //     if (return_code != 0) {
    //       ESP_LOGE(TAG, "Failed to build whitelist message\n");
    //       continue;
    //     }

    //     if (writeCharacteristic->writeValue(ephemeral_key_message_buffer,
    //                                         ephemeral_key_message_length)) {
    //       ESP_LOGI(TAG, "Waiting for keycard to be tapped...\n");
    //     }
    // }

    void TeslaBLECar::sendCommand(VCSEC_RKEAction_E action) {
      if (this->isAuthenticated == false) {
        ESP_LOGW(TAG, "Not authenticated yet");
        return;
      }

      unsigned char action_message_buffer[200];
      size_t action_message_buffer_length = 0;
      int return_code = m_pClient->BuildActionMessage(
          &action, action_message_buffer, &action_message_buffer_length);

      if (return_code != 0) {
        ESP_LOGE(TAG, "Failed to build action message");
        return;
      }

      auto err = esp_ble_gattc_write_char(this->parent()->get_gattc_if(), this->parent()->get_conn_id(), this->write_handle_, action_message_buffer_length, action_message_buffer, ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
      if (err) {
        ESP_LOGW(TAG, "Error sending write value to BLE gattc server, error=%d", err);
        return;
      }
      ESP_LOGD(TAG, "Command sent");
    }

    void TeslaBLECar::wakeVehicle() {
      ESP_LOGI(TAG, "Waking vehicle");
      this->sendCommand(VCSEC_RKEAction_E_RKE_ACTION_WAKE_VEHICLE);
    }

    void TeslaBLECar::unlockVehicle() {
      ESP_LOGI(TAG, "Unlocking vehicle");
      this->sendCommand(VCSEC_RKEAction_E_RKE_ACTION_UNLOCK);
    }

    void TeslaBLECar::lockVehicle() {
      ESP_LOGI(TAG, "Locking vehicle");
      this->sendCommand(VCSEC_RKEAction_E_RKE_ACTION_LOCK);
    }

    void TeslaBLECar::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                          esp_ble_gattc_cb_param_t *param)
    {
      ESP_LOGV(TAG, "GATTC event %d", event);
      switch (event)
      {
      case ESP_GATTC_OPEN_EVT:
      {
        if (param->open.status == ESP_GATT_OK)
        {
          ESP_LOGI(TAG, "Connected successfully!");
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
        auto *readChar = this->parent()->get_characteristic(this->service_uuid_, this->read_uuid_);
        if (readChar == nullptr)
        {
          ESP_LOGW(TAG, "No write characteristic found at service %s read %s",
                    this->service_uuid_.to_string().c_str(),
                    this->read_uuid_.to_string().c_str());
          break;
        }
        this->read_handle_ = readChar->handle;

        auto reg_status = esp_ble_gattc_register_for_notify(this->parent()->get_gattc_if(), this->parent()->get_remote_bda(), readChar->handle);
        if (reg_status) {
          ESP_LOGW(TAG, "esp_ble_gattc_register_for_notify failed, status=%d", reg_status);
          break;
        }

        auto *writeChar = this->parent()->get_characteristic(this->service_uuid_, this->write_uuid_);
        if (writeChar == nullptr)
        {
          ESP_LOGW(TAG, "No write characteristic found at service %s write %s",
                    this->service_uuid_.to_string().c_str(),
                    this->write_uuid_.to_string().c_str());
          break;
        }
        this->write_handle_ = writeChar->handle;
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
        // message_o.which_sub_message
        // pb_size_t which_sub_message;
        ESP_LOGD(TAG, "Which sub message: %u", message_o.which_sub_message);
        ESP_LOGD(TAG, "Has active key: %d", message_o.sub_message.activeKey.has_activeKey);
        TeslaBLE::DumpBuffer("\n", message_o.sub_message.activeKey.activeKey.publicKeySHA1, 4);
        ESP_LOGD(TAG, "Charge port close: %d", message_o.sub_message.capabilities.chargePortClose);
        ESP_LOGD(TAG, "Charge port open: %d", message_o.sub_message.capabilities.chargePortOpen);
        VCSEC_CommandStatus commandStatus = message_o.sub_message.commandStatus;
        // VCSEC_OperationStatus_E_OPERATIONSTATUS_OK = 0,
        // VCSEC_OperationStatus_E_OPERATIONSTATUS_WAIT = 1,
        // VCSEC_OperationStatus_E_OPERATIONSTATUS_ERROR = 2
        ESP_LOGD(TAG, "commandStatus.operationStatus: %d", commandStatus.operationStatus);
        ESP_LOGD(TAG, "commandStatus submessage: %u", commandStatus.which_sub_message);
        ESP_LOGD(TAG, "commandStatus signed message counter: %lu", commandStatus.sub_message.signedMessageStatus.counter);
        ESP_LOGD(TAG, "commandStatus whitelist operationStatus: %u", commandStatus.sub_message.whitelistOperationStatus.operationStatus);
        ESP_LOGD(TAG, "commandStatus whitelist has_signerOfOperation: %d", commandStatus.sub_message.whitelistOperationStatus.has_signerOfOperation);
        ESP_LOGD(TAG, "commandStatus whitelistOperationInformation: %d", commandStatus.sub_message.whitelistOperationStatus.whitelistOperationInformation);
        // pb_callback_t vin_callback = message_o.sub_message.vehicleInfo.VIN;
        // ESP_LOGD(TAG, "VIN: %s", vin);
        break;
      }

      default:
        break;
      }
    }

    // void TeslaBLECar::update()
    // {
    //   if (this->node_state != espbt::ClientState::ESTABLISHED)
    //   {
    //     if (!this->parent()->enabled)
    //     {
    //       ESP_LOGW(TAG, "Reconnecting to device");
    //       this->parent()->set_enabled(true);
    //       this->parent()->connect();
    //     }
    //     else
    //     {
    //       ESP_LOGW(TAG, "Connection in progress");
    //     }
    //   }
    // }

    // void TeslaBLECar::response_pending_()
    // {
    //   this->responses_pending_++;
    //   this->set_response_timeout_();
    // }

    // void TeslaBLECar::response_received_()
    // {
    //   if (--this->responses_pending_ == 0)
    //   {
    //     // This instance must not stay connected
    //     // so other clients can connect to it (e.g. the
    //     // mobile app).
    //     this->parent()->set_enabled(false);
    //   }
    // }

    // void TeslaBLECar::set_response_timeout_()
    // {
    //   this->set_timeout("response_timeout", 5 * 1000, [this]()
    //                     {
    // this->responses_pending_ = 1;
    // this->response_received_(); });
    // }

  } // namespace tesla_ble_car
} // namespace esphome
