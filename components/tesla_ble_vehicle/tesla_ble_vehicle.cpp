#include <client.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <pb_decode.h>
#include <tb_utils.h>

#include <keys.pb.h>
#include <universal_message.pb.h>
#include <vcsec.pb.h>

#include "esp_random.h"
#include "esphome/core/log.h"
#include "log.h"
#include "tesla_ble_vehicle.h"

namespace esphome
{
  namespace tesla_ble_vehicle
  {

    static const char *const TAG = "tesla_ble_vehicle";
    void TeslaBLEVehicle::dump_config()
    {
      ESP_LOGI(TAG, "Dumping Config");
    }
    TeslaBLEVehicle::TeslaBLEVehicle() : tesla_ble_client_(new TeslaBLE::Client{})
    {
      ESP_LOGI(TAG, "Constructing Tesla BLE Car component");
      this->isAuthenticated = false;
      this->init();

      this->service_uuid_ = espbt::ESPBTUUID::from_raw(SERVICE_UUID);
      this->read_uuid_ = espbt::ESPBTUUID::from_raw(READ_UUID);
      this->write_uuid_ = espbt::ESPBTUUID::from_raw(WRITE_UUID);
    }
    void TeslaBLEVehicle::setup()
    {
      ESP_LOGI(TAG, "Starting Tesla BLE Car component");

      ESP_LOGI(TAG, "Tesla BLE Car component started");
    }
    void TeslaBLEVehicle::set_vin(const char *vin)
    {
      tesla_ble_client_->setVIN(vin);
    }

    void TeslaBLEVehicle::loop()
    {
    }

    void TeslaBLEVehicle::init()
    {
      ESP_LOGI(TAG, "Initializing Tesla BLE Car component");
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
        int result_code = tesla_ble_client_->createPrivateKey();
        if (result_code != 0)
        {
          ESP_LOGI(TAG, "Failed create private key");
          esp_restart();
        }

        unsigned char private_key_buffer[228];
        size_t private_key_length = 0;
        tesla_ble_client_->getPrivateKey(private_key_buffer, sizeof(private_key_buffer),
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
            tesla_ble_client_->loadPrivateKey(private_key_buffer, required_private_key_size);
        if (result_code != 0)
        {
          ESP_LOGE(TAG, "Failed load private key");
          esp_restart();
        }

        ESP_LOGI(TAG, "Private key loaded successfully");
        TeslaBLE::dumpBuffer("\n", private_key_buffer, required_private_key_size);
      }

      size_t required_tesla_key_vcsec_size = 0;
      err = nvs_get_blob(this->storage_handle_, "tk_vcsec", NULL, &required_tesla_key_vcsec_size);
      if (required_tesla_key_vcsec_size > 0)
      {
        unsigned char tesla_key_vcsec_buffer[required_tesla_key_vcsec_size];

        err = nvs_get_blob(this->storage_handle_, "tk_vcsec", tesla_key_vcsec_buffer,
                           &required_tesla_key_vcsec_size);
        if (err != ESP_OK)
        {
          ESP_LOGE(TAG, "Failed read tesla VCSEC key from storage: %s",
                   esp_err_to_name(err));
          esp_restart();
        }

        int result_code =
            tesla_ble_client_->loadTeslaKey(false, tesla_key_vcsec_buffer, required_tesla_key_vcsec_size);

        if (result_code != 0)
        {
          ESP_LOGE(TAG, "Failed load tesla VCSEC key");
          esp_restart();
        }

        ESP_LOGI(TAG, "Tesla VCSEC key loaded successfully");
      }

      size_t required_tesla_key_infotainment_size = 0;
      err = nvs_get_blob(this->storage_handle_, "tk_infotainment", NULL, &required_tesla_key_infotainment_size);
      if (required_tesla_key_infotainment_size > 0)
      {
        unsigned char tesla_key_infotainment_buffer[required_tesla_key_infotainment_size];

        err = nvs_get_blob(this->storage_handle_, "tk_infotainment", tesla_key_infotainment_buffer,
                           &required_tesla_key_infotainment_size);
        if (err != ESP_OK)
        {
          ESP_LOGE(TAG, "Failed read tesla infotainment key from storage: %s",
                   esp_err_to_name(err));
          esp_restart();
        }

        int result_code =
            tesla_ble_client_->loadTeslaKey(true, tesla_key_infotainment_buffer, required_tesla_key_infotainment_size);

        if (result_code != 0)
        {
          ESP_LOGE(TAG, "Failed load tesla infotainment key");
          esp_restart();
        }

        ESP_LOGI(TAG, "Tesla infotainment key loaded successfully");
      }

      uint32_t counter_vcsec = 0;
      err = nvs_get_u32(this->storage_handle_, "c_vcsec", &counter_vcsec);
      if (err != ESP_OK)
      {
        ESP_LOGE(TAG, "Failed read VCSEC counter from storage: %s", esp_err_to_name(err));
      }

      if (counter_vcsec > 0)
      {
        tesla_ble_client_->session_vcsec_.setCounter(&counter_vcsec);
        ESP_LOGI(TAG, "Loaded old VCSEC counter %" PRIu32, counter_vcsec);
      }

      uint32_t counter_infotainment = 0;
      err = nvs_get_u32(this->storage_handle_, "c_infotainment", &counter_infotainment);
      if (err != ESP_OK)
      {
        ESP_LOGE(TAG, "Failed read INFOTAINMENT counter from storage: %s", esp_err_to_name(err));
      }

      if (counter_infotainment > 0)
      {
        tesla_ble_client_->session_infotainment_.setCounter(&counter_infotainment);
        ESP_LOGI(TAG, "Loaded old INFOTAINMENT counter %" PRIu32, counter_infotainment);
      }
    }

    void TeslaBLEVehicle::regenerateKey()
    {
      ESP_LOGI(TAG, "Regenerating key");
      int result_code = tesla_ble_client_->createPrivateKey();
      if (result_code != 0)
      {
        ESP_LOGE(TAG, "Failed create private key");
        return;
      }

      unsigned char private_key_buffer[228];
      size_t private_key_length = 0;
      tesla_ble_client_->getPrivateKey(private_key_buffer, sizeof(private_key_buffer),
                                       &private_key_length);

      esp_err_t result_flash_init = nvs_flash_init();
      if (result_flash_init != ESP_OK)
      {
        ESP_LOGE(TAG, "Failed to initialize flash: %s", esp_err_to_name(result_flash_init));
      }

      esp_err_t result_nvs_open = nvs_open("storage", NVS_READWRITE, &this->storage_handle_);
      if (result_nvs_open != ESP_OK)
      {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(result_nvs_open));
      }

      esp_err_t result_nvs_set_blob = nvs_set_blob(this->storage_handle_, "private_key",
                                                   private_key_buffer, private_key_length);
      if (result_nvs_set_blob != ESP_OK)
      {
        ESP_LOGE(TAG, "Failed commit storage: %s", esp_err_to_name(result_nvs_set_blob));
      }

      esp_err_t result_nvs_commit = nvs_commit(this->storage_handle_);
      if (result_nvs_commit != ESP_OK)
      {
        ESP_LOGE(TAG, "Failed commit storage: %s", esp_err_to_name(result_nvs_commit));
      }

      ESP_LOGI(TAG, "Private key successfully created");
      TeslaBLE::dumpBuffer("\n", private_key_buffer, private_key_length);
    }

    void TeslaBLEVehicle::startPair()
    {
      ESP_LOGI(TAG, "Starting pairing");
      ESP_LOGI(TAG, "Not authenticated yet, building whitelist message");
      unsigned char whitelist_message_buffer[tesla_ble_client_->MAX_BLE_MESSAGE_SIZE];
      size_t whitelist_message_length = 0;
      // support for wake command added to CHARGING_MANAGER_ROLE in 2024.20.6.2 (not sure?)
      // https://github.com/teslamotors/vehicle-command/issues/232#issuecomment-2181503570
      int return_code = tesla_ble_client_->buildWhiteListMessage(Keys_Role_ROLE_CHARGING_MANAGER, VCSEC_KeyFormFactor_KEY_FORM_FACTOR_CLOUD_KEY, whitelist_message_buffer, &whitelist_message_length);

      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed to build whitelist message");
        return;
      }
      ESP_LOGV(TAG, "Whitelist message length: %d", whitelist_message_length);
      writeBLE(whitelist_message_buffer, whitelist_message_length, ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
      ESP_LOGI(TAG, "Please tap your card on the reader now..");
      // this->isAuthenticated = true;
    }

    void TeslaBLEVehicle::sendEphemeralKeyRequest(UniversalMessage_Domain domain)
    {
      unsigned char message_buffer[tesla_ble_client_->MAX_BLE_MESSAGE_SIZE];
      size_t message_length = 0;
      int return_code = tesla_ble_client_->buildEphemeralKeyMessage(
          domain,
          message_buffer,
          &message_length);

      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed to build whitelist message");
        return;
      }

      writeBLE(message_buffer, message_length, ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
    }

    void TeslaBLEVehicle::sendKeySummary()
    {
      ESP_LOGD(TAG, "sendKeySummary");
      unsigned char message_buffer[tesla_ble_client_->MAX_BLE_MESSAGE_SIZE];
      size_t message_length = 0;
      int return_code = tesla_ble_client_->buildKeySummary(
          message_buffer,
          &message_length);
      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed to build KeySummary");
        return;
      }

      writeBLE(message_buffer, message_length, ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
    }

    int TeslaBLEVehicle::writeBLE(const unsigned char *message_buffer, size_t message_length,
                                  esp_gatt_write_type_t write_type, esp_gatt_auth_req_t auth_req)
    {
      ESP_LOGD(TAG, "BLE TX:");
      ESP_LOG_BUFFER_HEX(TAG, message_buffer, message_length);
      // BLE MTU is 23 bytes, so we need to split the message into chunks (20 bytes as in vehicle_command)
      for (size_t i = 0; i < message_length; i += BLOCK_LENGTH)
      {
        size_t chunkLength = BLOCK_LENGTH;
        if (i + chunkLength > message_length)
        {
          chunkLength = message_length - i;
        }
        unsigned char chunk[chunkLength];
        std::memcpy(chunk, message_buffer + i, chunkLength);
        auto err = esp_ble_gattc_write_char(this->parent()->get_gattc_if(), this->parent()->get_conn_id(), this->write_handle_, chunkLength, chunk, write_type, auth_req);
        if (err)
        {
          ESP_LOGW(TAG, "Error sending write value to BLE gattc server, error=%d", err);
          return 1;
        }
      }
      ESP_LOGD(TAG, "Command sent.");
      return 0;
    }

    void TeslaBLEVehicle::sendCommand(VCSEC_RKEAction_E action)
    {
      if (tesla_ble_client_->session_vcsec_.isAuthenticated == false)
      {
        ESP_LOGW(TAG, "Not authenticated yet");
        this->sendEphemeralKeyRequest(UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY);
        ESP_LOGI(TAG, "Ephemeral key sent to VEHICLE_SECURITY");
        return;
      }

      unsigned char action_message_buffer[tesla_ble_client_->MAX_BLE_MESSAGE_SIZE];
      size_t action_message_buffer_length = 0;
      int return_code = tesla_ble_client_->buildVCSECActionMessage(action, action_message_buffer, &action_message_buffer_length);

      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed to build action message");
        return;
      }

      writeBLE(action_message_buffer, action_message_buffer_length, ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
    }

    void TeslaBLEVehicle::wakeVehicle()
    {
      ESP_LOGI(TAG, "Waking vehicle");
      this->sendCommand(VCSEC_RKEAction_E_RKE_ACTION_WAKE_VEHICLE);
    }

    void TeslaBLEVehicle::lockVehicle()
    {
      ESP_LOGI(TAG, "Locking vehicle");
      this->sendCommand(VCSEC_RKEAction_E_RKE_ACTION_LOCK);
    }

    void TeslaBLEVehicle::unlockVehicle()
    {
      ESP_LOGI(TAG, "Unlocking vehicle");
      this->sendCommand(VCSEC_RKEAction_E_RKE_ACTION_UNLOCK);
    }

    void TeslaBLEVehicle::sendInfoStatus()
    {
      ESP_LOGD(TAG, "sendInfoStatus");
      unsigned char message_buffer[tesla_ble_client_->MAX_BLE_MESSAGE_SIZE];
      size_t message_length = 0;
      int return_code = tesla_ble_client_->buildVCSECInformationRequestMessage(
          VCSEC_InformationRequestType_INFORMATION_REQUEST_TYPE_GET_STATUS,
          message_buffer,
          &message_length);
      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed to build VCSECInformationRequestMessage");
        return;
      }

      writeBLE(message_buffer, message_length, ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
    }

    void TeslaBLEVehicle::setChargingSwitch(bool isOn)
    {
      if (tesla_ble_client_->session_infotainment_.isAuthenticated == false)
      {
        ESP_LOGW(TAG, "Not authenticated yet, try again in a few seconds");
        this->sendEphemeralKeyRequest(UniversalMessage_Domain_DOMAIN_INFOTAINMENT);
        ESP_LOGI(TAG, "Ephemeral key sent to INFOTAINMENT");
        return;
      }

      ESP_LOGI(TAG, "Setting charging switch to %s", isOn ? "ON" : "OFF");
      unsigned char charge_message_buffer[tesla_ble_client_->MAX_BLE_MESSAGE_SIZE];
      size_t charge_message_length = 0;
      int return_code = tesla_ble_client_->buildChargingSwitchMessage(isOn, charge_message_buffer, &charge_message_length);

      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed to build charge message");
        return;
      }

      writeBLE(charge_message_buffer, charge_message_length, ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
    }

    void TeslaBLEVehicle::setChargingAmps(int input_amps)
    {
      if (tesla_ble_client_->session_infotainment_.isAuthenticated == false)
      {
        ESP_LOGW(TAG, "Not authenticated yet, try again in a few seconds");
        this->sendEphemeralKeyRequest(UniversalMessage_Domain_DOMAIN_INFOTAINMENT);
        ESP_LOGI(TAG, "Ephemeral key sent to INFOTAINMENT");
        return;
      }

      // convert to uint32_t
      uint32_t amps = input_amps;

      ESP_LOGI(TAG, "Setting charge amps to %ld", amps);
      unsigned char charge_message_buffer[tesla_ble_client_->MAX_BLE_MESSAGE_SIZE];
      size_t charge_message_length;
      int return_code = tesla_ble_client_->buildChargingAmpsMessage(amps, charge_message_buffer, &charge_message_length);

      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed to build charge amps message");
        return;
      }

      writeBLE(charge_message_buffer, charge_message_length, ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
    }

    void TeslaBLEVehicle::setChargingLimit(int input_percent)
    {
      if (tesla_ble_client_->session_infotainment_.isAuthenticated == false)
      {
        ESP_LOGW(TAG, "Not authenticated yet, try again in a few seconds");
        this->sendEphemeralKeyRequest(UniversalMessage_Domain_DOMAIN_INFOTAINMENT);
        ESP_LOGI(TAG, "Ephemeral key sent to INFOTAINMENT");
        return;
      }

      // convert to uint32_t
      uint32_t percent = input_percent;

      ESP_LOGI(TAG, "Setting charge limit to %ld", percent);
      unsigned char charge_message_buffer[tesla_ble_client_->MAX_BLE_MESSAGE_SIZE];
      size_t charge_message_length;
      int return_code = tesla_ble_client_->buildChargingSetLimitMessage(percent, charge_message_buffer, &charge_message_length);

      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed to build charge limit message");
        return;
      }

      writeBLE(charge_message_buffer, charge_message_length, ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
    }

    void TeslaBLEVehicle::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                              esp_ble_gattc_cb_param_t *param)
    {
      ESP_LOGV(TAG, "GATTC event %d", event);
      switch (event)
      {
      case ESP_GATTC_CONNECT_EVT:
      {
        // this->handle_ = param->connect.conn_id;
        break;
      }

      case ESP_GATTC_OPEN_EVT:
      {
        if (param->open.status == ESP_GATT_OK)
        {
          ESP_LOGI(TAG, "Connected successfully!");

          // generate random connection id 16 bytes
          pb_byte_t connection_id[16];
          for (int i = 0; i < 16; i++)
          {
            connection_id[i] = esp_random();
          }
          ESP_LOGI(TAG, "Connection ID");
          ESP_LOG_BUFFER_HEX(TAG, connection_id, 16);
          tesla_ble_client_->setConnectionID(connection_id);
        }
        break;
      }

      case ESP_GATTC_SRVC_CHG_EVT:
      {
        esp_bd_addr_t bda;
        memcpy(bda, param->srvc_chg.remote_bda, sizeof(esp_bd_addr_t));
        ESP_LOGI(TAG, "ESP_GATTC_SRVC_CHG_EVT, bd_addr:");
        ESP_LOG_BUFFER_HEX(TAG, bda, sizeof(esp_bd_addr_t));
        break;
      }
      case ESP_GATTC_DISCONNECT_EVT:
      {
        this->handle_ = 0;
        this->read_handle_ = 0;
        this->write_handle_ = 0;
        this->isAuthenticated = false;
        tesla_ble_client_->session_vcsec_.setIsAuthenticated(false);
        tesla_ble_client_->session_infotainment_.setIsAuthenticated(false);
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
        if (reg_status)
        {
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

        ESP_LOGI(TAG, "Read char success ");
        break;
      }

      case ESP_GATTS_READ_EVT:
      {
        if (param->read.conn_id != this->parent()->get_conn_id())
          break;
        if (param->read.status != ESP_GATT_OK)
        {
          ESP_LOGW(TAG, "Error reading char at handle %d, status=%d", param->read.handle, param->read.status);
          break;
        }
        ESP_LOGI(TAG, "ESP_GATTS_READ_EVT ");
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
        ESP_LOGI(TAG, "ESP_GATTC_READ_CHAR_EVT ");
        break;
      }

      case ESP_GATTC_REG_FOR_NOTIFY_EVT:
      {
        if (param->reg_for_notify.status != ESP_GATT_OK)
        {
          ESP_LOGE(TAG, "reg for notify failed, error status = %x", param->reg_for_notify.status);
          break;
        }
        this->node_state = espbt::ClientState::ESTABLISHED;

        // do things here
        this->sendKeySummary();

        unsigned char private_key_buffer[228];
        size_t private_key_length = 0;
        tesla_ble_client_->getPrivateKey(private_key_buffer, sizeof(private_key_buffer),
                                         &private_key_length);
        ESP_LOGI(TAG, "Private key");
        ESP_LOG_BUFFER_HEX(TAG, private_key_buffer, private_key_length);
        // private key in PEM format (char)
        ESP_LOGI(TAG, "Private key (char): %s", private_key_buffer);

        unsigned char public_key_buffer[65];
        size_t public_key_length;
        tesla_ble_client_->getPublicKey(public_key_buffer, &public_key_length);
        ESP_LOGI(TAG, "Public key");
        ESP_LOG_BUFFER_HEX(TAG, public_key_buffer, public_key_length);
        // public key in PEM format (char)
        ESP_LOGI(TAG, "Public key (char): %s", public_key_buffer);

        this->sendEphemeralKeyRequest(UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY);
        ESP_LOGI(TAG, "Ephemeral key sent to VEHICLE_SECURITY");
        this->sendEphemeralKeyRequest(UniversalMessage_Domain_DOMAIN_INFOTAINMENT);
        ESP_LOGI(TAG, "Ephemeral key sent to INFOTAINMENT");

        break;
      }

      case ESP_GATTC_WRITE_DESCR_EVT:
      {
        if (param->write.conn_id != this->parent()->get_conn_id())
          break;
        if (param->write.status != ESP_GATT_OK)
        {
          ESP_LOGW(TAG, "Error writing descriptor at handle %d, status=%d", param->write.handle, param->write.status);
          break;
        }
        break;
      }
      case ESP_GATTC_WRITE_CHAR_EVT:

        if (param->write.status != ESP_GATT_OK)
        {
          ESP_LOGE(TAG, "write char failed, error status = %x", param->write.status);
          break;
        }
        ESP_LOGI(TAG, "Write char success");
        break;

      case ESP_GATTC_NOTIFY_EVT:
      {
        if (param->notify.conn_id != this->parent()->get_conn_id())
        {
          ESP_LOGW(TAG, "Received notify from unknown connection");
          return;
        }
        ESP_LOGD(TAG, "%d: - RAM left %ld", __LINE__, esp_get_free_heap_size());
        ESP_LOGD(TAG, "BLE RX:");
        // ESP_LOGI(TAG, "ESP_GATTC_NOTIFY_EVT, receive notify value:");
        ESP_LOG_BUFFER_HEX(TAG, param->notify.value, param->notify.value_len);

        // if (this->isAuthenticated == false)
        // {
        //   ESP_LOGW(TAG, "Not authenticated yet, sending ephemeral key");
        //   // this->isAuthenticated = true;
        // }

        UniversalMessage_RoutableMessage message = UniversalMessage_RoutableMessage_init_default;
        ESP_LOGD(TAG, "Receiving message in chunks");
        // append to buffer
        // Ensure the buffer has enough space
        if (this->current_size + param->notify.value_len > this->read_buffer.size())
        {
          ESP_LOGD(TAG, "Resizing read buffer");
          this->read_buffer.resize(this->current_size + param->notify.value_len);
        }

        // Append the new data
        std::memcpy(this->read_buffer.data() + this->current_size, param->notify.value, param->notify.value_len);
        this->current_size += param->notify.value_len;

        int return_code = tesla_ble_client_->parseUniversalMessageBLE(this->read_buffer.data(), this->current_size, &message);
        if (return_code != 0)
        {
          ESP_LOGE(TAG, "Failed to parse incoming message (maybe chunk?)");
          return;
        }
        ESP_LOGD(TAG, "Parsed UniversalMessage");
        // clear read buffer
        this->current_size = 0;
        this->read_buffer.clear();         // This will set the size to 0 and free unused memory
        this->read_buffer.shrink_to_fit(); // This will reduce the capacity to fit the size

        if (not message.has_from_destination)
        {
          ESP_LOGW(TAG, "Dropping message with missing source (probably broadcast message)");
          return;
        }
        UniversalMessage_Domain domain = message.from_destination.sub_destination.domain;

        if (not message.has_from_destination)
        {
          ESP_LOGW(TAG, "Dropping message with missing destination");
          return;
        }

        switch (message.to_destination.which_sub_destination)
        {
        case UniversalMessage_Destination_domain_tag:
        {
          ESP_LOGI(TAG, "Continuing message to %s", domain_to_string(message.to_destination.sub_destination.domain));
          break;
        }
        case UniversalMessage_Destination_routing_address_tag:
        {
          // Continue
          ESP_LOGI(TAG, "Continuing message with routing address");
          break;
        }
        default:
        {
          ESP_LOGW(TAG, "Dropping message with unrecognized destination type");
          return;
        }
        }

        // log error if present in message
        if (message.has_signedMessageStatus)
        {
          ESP_LOGE(TAG, "Received error message from domain %s", domain_to_string(domain));
          log_message_status(TAG, &message.signedMessageStatus);
          return;
        }

        if (message.which_payload == UniversalMessage_RoutableMessage_session_info_tag)
        {
          ESP_LOGI(TAG, "Received session info response from domain %s", domain_to_string(domain));
          // log_routable_message(TAG, &message);

          // parse session info
          Signatures_SessionInfo session_info = Signatures_SessionInfo_init_default;
          int return_code = tesla_ble_client_->parsePayloadSessionInfo(&message.payload.session_info, &session_info);
          if (return_code != 0)
          {
            ESP_LOGE(TAG, "Failed to parse session info response");
            return;
          }
          ESP_LOGD(TAG, "Parsed session info response");
          log_session_info(TAG, &session_info);
          ESP_LOGD(TAG, "Received new counter from the car: %" PRIu32, session_info.counter);
          ESP_LOGD(TAG, "Received new expires at from the car: %" PRIu32, session_info.clock_time);
          ESP_LOGD(TAG, "Received new epoch from the car");
          ESP_LOG_BUFFER_HEX(TAG, session_info.epoch, sizeof session_info.epoch);

          // signer.timeZero = generatedAt.Add(-time.Duration(info.ClockTime) * time.Second)

          // generatedAt = now
          uint32_t generated_at = std::time(nullptr);
          uint32_t time_zero = generated_at - session_info.clock_time;

          switch (domain)
          {
          case UniversalMessage_Domain_DOMAIN_INFOTAINMENT:
          {
            ESP_LOGI(TAG, "Received session info from infotainment domain");
            tesla_ble_client_->session_infotainment_.setCounter(&session_info.counter);
            tesla_ble_client_->session_infotainment_.setExpiresAt(&session_info.clock_time);
            tesla_ble_client_->session_infotainment_.setEpoch(session_info.epoch);
            tesla_ble_client_->session_infotainment_.setTimeZero(&time_zero);

            ESP_LOGD(TAG, "Loading infotainment public key from car");
            // convert pb Failed to parse incoming message
            int result_code_infotainment =
                tesla_ble_client_->loadTeslaKey(true, session_info.publicKey.bytes, session_info.publicKey.size);

            if (result_code_infotainment != 0)
            {
              ESP_LOGE(TAG, "Failed load tesla infotainment key");
              return;
            }

            esp_err_t err = nvs_open("storage", NVS_READWRITE, &this->storage_handle_);
            if (err != ESP_OK)
            {
              ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
            }
            err = nvs_set_blob(this->storage_handle_, "tk_infotainment",
                               &session_info.publicKey.bytes,
                               session_info.publicKey.size);

            if (err != ESP_OK)
            {
              ESP_LOGE(TAG, "Failed to save tesla infotainment key: %s", esp_err_to_name(err));
              return;
            }
            err = nvs_commit(this->storage_handle_);
            if (err != ESP_OK)
            {
              ESP_LOGE(TAG, "Failed commit storage: %s", esp_err_to_name(err));
            }

            err = nvs_set_u32(this->storage_handle_, "counter", session_info.counter);
            if (err != ESP_OK)
            {
              ESP_LOGE(TAG, "Failed to save infotainment counter: %s", esp_err_to_name(err));
            }

            break;
          }
          case UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY:
          {
            ESP_LOGI(TAG, "Received session info from VCSEC domain");
            tesla_ble_client_->session_vcsec_.setCounter(&session_info.counter);
            tesla_ble_client_->session_vcsec_.setExpiresAt(&session_info.clock_time);
            tesla_ble_client_->session_vcsec_.setEpoch(session_info.epoch);
            tesla_ble_client_->session_vcsec_.setTimeZero(&time_zero);

            ESP_LOGD(TAG, "Loading VCSEC public key from car");
            // convert pb Failed to parse incoming message
            int result_code_vcsec =
                tesla_ble_client_->loadTeslaKey(false, session_info.publicKey.bytes, session_info.publicKey.size);

            if (result_code_vcsec != 0)
            {
              ESP_LOGE(TAG, "Failed load tesla VCSEC key");
              return;
            }

            esp_err_t err = nvs_open("storage", NVS_READWRITE, &this->storage_handle_);
            if (err != ESP_OK)
            {
              ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
            }
            err = nvs_set_blob(this->storage_handle_, "tk_vcsec",
                               &session_info.publicKey.bytes,
                               session_info.publicKey.size);
            if (err != ESP_OK)
            {
              ESP_LOGE(TAG, "Failed to save tesla VCSEC key: %s", esp_err_to_name(err));
              return;
            }
            err = nvs_commit(this->storage_handle_);
            if (err != ESP_OK)
            {
              ESP_LOGE(TAG, "Failed commit storage: %s", esp_err_to_name(err));
            }

            err = nvs_set_u32(this->storage_handle_, "counter", session_info.counter);
            if (err != ESP_OK)
            {
              ESP_LOGE(TAG, "Failed to save infotainment counter: %s", esp_err_to_name(err));
            }
            break;
          }
          default:
            ESP_LOGW(TAG, "Received session info from unknown domain");
            break;
          }
          break;
        }
        else if (message.has_from_destination == false)
        {
          ESP_LOGE(TAG, "Received message without from_destination");
          return;
        }

        log_routable_message(TAG, &message);
        switch (message.from_destination.which_sub_destination)
        {
        case UniversalMessage_Destination_domain_tag:
        {
          ESP_LOGI(TAG, "Received message for %s", domain_to_string(message.to_destination.sub_destination.domain));

          switch (message.from_destination.sub_destination.domain)
          {
          case UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY:
          {
            ESP_LOGI(TAG, "Received message from VCSEC domain");
            VCSEC_FromVCSECMessage vcsec_message = VCSEC_FromVCSECMessage_init_default;
            int return_code = tesla_ble_client_->parseFromVCSECMessage(&message.payload.protobuf_message_as_bytes, &vcsec_message);
            if (return_code != 0)
            {
              ESP_LOGE(TAG, "Failed to parse incoming message");
              return;
            }
            ESP_LOGD(TAG, "Parsed VCSEC message");

            switch (vcsec_message.which_sub_message)
            {
            case VCSEC_FromVCSECMessage_vehicleStatus_tag:
            {
              ESP_LOGI(TAG, "Received vehicle status");
              ESP_LOGI(TAG, "  vehicleSleepStatus: %d", vcsec_message.sub_message.vehicleStatus.vehicleSleepStatus);
              log_vehicle_status(TAG, &vcsec_message.sub_message.vehicleStatus);
              break;
            }
            case VCSEC_FromVCSECMessage_commandStatus_tag:
            {
              ESP_LOGI(TAG, "Received command status");
              break;
            }
            case VCSEC_FromVCSECMessage_whitelistInfo_tag:
            {
              ESP_LOGI(TAG, "Received whitelist info");
              break;
            }
            case VCSEC_FromVCSECMessage_whitelistEntryInfo_tag:
            {
              ESP_LOGI(TAG, "Received whitelist entry info");
              break;
            }
            case VCSEC_FromVCSECMessage_nominalError_tag:
            {
              ESP_LOGI(TAG, "Received nominal error");
              break;
            }
            default:
            {
              // probably information request with public key
              VCSEC_InformationRequest info_message = VCSEC_InformationRequest_init_default;
              int return_code = tesla_ble_client_->parseVCSECInformationRequest(&message.payload.protobuf_message_as_bytes, &info_message);
              if (return_code != 0)
              {
                ESP_LOGE(TAG, "Failed to parse incoming VSSEC message");
                return;
              }
              ESP_LOGD(TAG, "Parsed VCSEC InformationRequest message");
              // log received public key
              ESP_LOGD(TAG, "InformationRequest public key");
              ESP_LOG_BUFFER_HEX(TAG, info_message.key.publicKey.bytes, info_message.key.publicKey.size);
              return;
            }
            break;
            }
            break;
          }

          case UniversalMessage_Domain_DOMAIN_INFOTAINMENT:
          {
            CarServer_Action carserver_action = CarServer_Action_init_default;
            int return_code = tesla_ble_client_->parsePayloadCarServerAction(&message.payload.protobuf_message_as_bytes, &carserver_action);
            if (return_code != 0)
            {
              ESP_LOGE(TAG, "Failed to parse incoming message");
              return;
            }
            ESP_LOGI(TAG, "Parsed CarServerAction");

            switch (carserver_action.action_msg.vehicleAction.which_vehicle_action_msg)
            {
            case CarServer_ActionStatus_result_tag:
            {
              ESP_LOGI(TAG, "Received action result");
              break;
            }
            default:
            {
              ESP_LOGI(TAG, "Received message from unknown payload %d", message.which_payload);
              break;
            }
            }
            break;
          }
          default:
          {
            ESP_LOGI(TAG, "Received message from unknown domain %d", message.from_destination.sub_destination.domain);
            break;
          }
          break;
          }
          break;
        }

        case UniversalMessage_Destination_routing_address_tag:
        {
          ESP_LOGI(TAG, "Received message from routing address");
          break;
        }
        default:
        {
          ESP_LOGI(TAG, "Received message from unknown domain %d", message.from_destination.sub_destination.domain);
          break;
        }
        break;
        }
        break;
      }

      default:
        ESP_LOGI(TAG, "Unhandled GATTC event %d", event);
        break;
      }
    }
  } // namespace tesla_ble_vehicle
} // namespace esphome
