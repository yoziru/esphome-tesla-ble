#include <esp_random.h>
#include <esphome/core/helpers.h>
#include <esphome/core/log.h>
#include <nvs_flash.h>
#include <pb_decode.h>
#include <cstring>
#include <ctime>

#include <car_server.pb.h>
#include <client.h>
#include <errors.h>
#include <peer.h>
#include <keys.pb.h>
#include <tb_utils.h>
#include <universal_message.pb.h>
#include <vcsec.pb.h>

#include "log.h"
#include "tesla_ble_vehicle.h"

namespace esphome
{
  namespace tesla_ble_vehicle
  {

    static const char *const TAG = "tesla_ble_vehicle";
    static const char *nvs_key_id_infotainment = "tk_infotainment";
    static const char *nvs_key_id_vcsec = "tk_vcsec";
    static const char *nvs_counter_id_infotainment = "c_infotainment";
    static const char *nvs_counter_id_vcsec = "c_vcsec";

    void TeslaBLEVehicle::dump_config()
    {
      ESP_LOGD(TAG, "Dumping Config");
    }
    TeslaBLEVehicle::TeslaBLEVehicle() : tesla_ble_client_(new TeslaBLE::Client{})
    {
      ESP_LOGD(TAG, "Constructing Tesla BLE Vehicle component");
      this->init();

      this->service_uuid_ = espbt::ESPBTUUID::from_raw(SERVICE_UUID);
      this->read_uuid_ = espbt::ESPBTUUID::from_raw(READ_UUID);
      this->write_uuid_ = espbt::ESPBTUUID::from_raw(WRITE_UUID);
    }
    void TeslaBLEVehicle::setup()
    {
    }
    void TeslaBLEVehicle::set_vin(const char *vin)
    {
      tesla_ble_client_->setVIN(vin);
    }

    void TeslaBLEVehicle::loop()
    {
    }

    void TeslaBLEVehicle::update()
    {
      ESP_LOGD(TAG, "Updating Tesla BLE Vehicle component..");
      if (this->node_state == espbt::ClientState::ESTABLISHED)
      {
        ESP_LOGD(TAG, "Polling for vehicle status..");
        int return_code = this->sendInfoStatus();
        if (return_code != 0)
        {
          ESP_LOGE(TAG, "Failed to send info status");
          return;
        }
        ESP_LOGD(TAG, "Sent info status");
        return;
      }
      else
      {
        ESP_LOGD(TAG, "Vehicle is not connected");
        // set sleep status to unknown if it's not yet
        if (this->asleepSensor->has_state() == true)
        {

          this->updateAsleepState(NAN);
        }
      }
    }

    void TeslaBLEVehicle::init()
    {
      ESP_LOGI(TAG, "Initializing Tesla BLE Vehicle component");
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
          ESP_LOGE(TAG, "Failed create private key");
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
      }

      size_t required_tesla_key_vcsec_size = 0;
      err = nvs_get_blob(this->storage_handle_, nvs_key_id_vcsec, NULL, &required_tesla_key_vcsec_size);
      if (required_tesla_key_vcsec_size > 0)
      {
        unsigned char tesla_key_vcsec_buffer[required_tesla_key_vcsec_size];

        err = nvs_get_blob(this->storage_handle_, nvs_key_id_vcsec, tesla_key_vcsec_buffer,
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
      err = nvs_get_blob(this->storage_handle_, nvs_key_id_infotainment, NULL, &required_tesla_key_infotainment_size);
      if (required_tesla_key_infotainment_size > 0)
      {
        unsigned char tesla_key_infotainment_buffer[required_tesla_key_infotainment_size];

        err = nvs_get_blob(this->storage_handle_, nvs_key_id_infotainment, tesla_key_infotainment_buffer,
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
      err = nvs_get_u32(this->storage_handle_, nvs_counter_id_vcsec, &counter_vcsec);
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
      err = nvs_get_u32(this->storage_handle_, nvs_counter_id_infotainment, &counter_infotainment);
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
    }

    int TeslaBLEVehicle::startPair()
    {
      ESP_LOGI(TAG, "Starting pairing");
      ESP_LOGI(TAG, "Not authenticated yet, building whitelist message");
      unsigned char whitelist_message_buffer[tesla_ble_client_->MAX_BLE_MESSAGE_SIZE];
      size_t whitelist_message_length = 0;
      // support for wake command added to ROLE_CHARGING_MANAGER in 2024.26.x (not sure?)
      // https://github.com/teslamotors/vehicle-command/issues/232#issuecomment-2181503570
      // TODO: change back to ROLE_CHARGING_MANAGER when it's supported
      int return_code = tesla_ble_client_->buildWhiteListMessage(Keys_Role_ROLE_OWNER, VCSEC_KeyFormFactor_KEY_FORM_FACTOR_CLOUD_KEY, whitelist_message_buffer, &whitelist_message_length);
      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed to build whitelist message");
        return return_code;
      }
      ESP_LOGV(TAG, "Whitelist message length: %d", whitelist_message_length);

      return_code = writeBLE(whitelist_message_buffer, whitelist_message_length, ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed to send whitelist message");
        return return_code;
      }
      ESP_LOGI(TAG, "Please tap your card on the reader now..");
      return 0;
    }

    int TeslaBLEVehicle::sendSessionInfoRequest(UniversalMessage_Domain domain)
    {
      unsigned char message_buffer[tesla_ble_client_->MAX_BLE_MESSAGE_SIZE];
      size_t message_length = 0;
      int return_code = tesla_ble_client_->buildSessionInfoRequestMessage(
          domain,
          message_buffer,
          &message_length);

      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed to build whitelist message");
        return return_code;
      }

      return_code = writeBLE(message_buffer, message_length, ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed to send SessionInfoRequest");
        return return_code;
      }
      return 0;
    }

    int TeslaBLEVehicle::writeBLE(const unsigned char *message_buffer, size_t message_length,
                                  esp_gatt_write_type_t write_type, esp_gatt_auth_req_t auth_req)
    {
      ESP_LOGD(TAG, "BLE TX: %s", format_hex(message_buffer, message_length).c_str());
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

    int TeslaBLEVehicle::sendCommand(VCSEC_RKEAction_E action)
    {
      int return_code = this->vcsecPreflightCheck();
      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed VCSEC preflight check");
        return return_code;
      }

      unsigned char action_message_buffer[tesla_ble_client_->MAX_BLE_MESSAGE_SIZE];
      size_t action_message_buffer_length = 0;
      return_code = tesla_ble_client_->buildVCSECActionMessage(action, action_message_buffer, &action_message_buffer_length);
      if (return_code != 0)
      {
        if (return_code == TeslaBLE::TeslaBLE_Status_E_ERROR_INVALID_SESSION)
        {
          tesla_ble_client_->session_vcsec_.setIsAuthenticated(false);
        }
        ESP_LOGE(TAG, "Failed to build action message");
        return return_code;
      }

      return_code = writeBLE(action_message_buffer, action_message_buffer_length, ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed to send action message");
        return return_code;
      }
      return 0;
    }

    int TeslaBLEVehicle::wakeVehicle()
    {
      ESP_LOGI(TAG, "Waking vehicle");
      if (this->asleepSensor->state == false)
      {
        ESP_LOGI(TAG, "Vehicle is already awake");
        return 0;
      }

      int return_code = this->sendCommand(VCSEC_RKEAction_E_RKE_ACTION_WAKE_VEHICLE);
      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed to send wake command");
        return return_code;
      }

      // get updated state after waking vehicle
      ESP_LOGD(TAG, "Getting updated VCSEC state");
      return_code = this->sendInfoStatus();
      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed to send info status");
        return return_code;
      }
      return 0;
    }

    int TeslaBLEVehicle::sendInfoStatus()
    {
      int return_code = this->vcsecPreflightCheck();
      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed VCSEC preflight check");
        return return_code;
      }
      ESP_LOGD(TAG, "sendInfoStatus");
      unsigned char message_buffer[tesla_ble_client_->MAX_BLE_MESSAGE_SIZE];
      size_t message_length = 0;
      return_code = tesla_ble_client_->buildVCSECInformationRequestMessage(
          VCSEC_InformationRequestType_INFORMATION_REQUEST_TYPE_GET_STATUS,
          message_buffer,
          &message_length);
      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed to build VCSECInformationRequestMessage");
        return return_code;
      }

      return_code = writeBLE(message_buffer, message_length, ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed to send VCSECInformationRequestMessage");
        return return_code;
      }
      return 0;
    }

    int TeslaBLEVehicle::vcsecPreflightCheck()
    {
      ESP_LOGD(TAG, "Running VCSEC preflight checks..");
      // make sure we're connected
      if (this->node_state != espbt::ClientState::ESTABLISHED)
      {
        ESP_LOGW(TAG, "Not connected yet");
        return 1;
      }
      // make sure we're authenticated
      if (tesla_ble_client_->session_vcsec_.isAuthenticated == false)
      {
        ESP_LOGW(TAG, "Not authenticated yet, try again in a few seconds");
        int return_code = this->sendSessionInfoRequest(UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY);
        if (return_code != 0)
        {
          ESP_LOGE(TAG, "Failed to send SessionInfoRequest");
          return return_code;
        }
        ESP_LOGI(TAG, "SessionInfoRequest sent to VEHICLE_SECURITY");
        return 0;
      }
      // all good
      ESP_LOGD(TAG, "VCSEC preflight check passed");
      return 0;
    }

    int TeslaBLEVehicle::infotainmentPreflightCheck()
    {
      ESP_LOGD(TAG, "Running INFOTAINMENT preflight checks..");
      int return_code = this->vcsecPreflightCheck();
      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed VCSEC preflight check");
        return return_code;
      }

      ESP_LOGD(TAG, "Checking if car is awake..");
      // first handle case where state is Unknown (NAN) (e.g. on boot)
      if (this->asleepSensor->has_state() == false)
      {
        ESP_LOGI(TAG, "Car state is unknown, sending info status");
        return_code = this->sendInfoStatus();
        if (return_code != 0)
        {
          ESP_LOGE(TAG, "Failed to send info status");
          return return_code;
        }
      }

      if (this->asleepSensor->state == false)
      {
        ESP_LOGD(TAG, "Car is awake");
      }
      else
      {
        ESP_LOGW(TAG, "Car is asleep, waking up");
        return_code = this->wakeVehicle();
        if (return_code != 0)
        {
          ESP_LOGE(TAG, "Failed to wake vehicle");
          return return_code;
        }
        // fail so that we can retry after waking up
        return 1;
      }

      // make sure we're authenticated
      if (tesla_ble_client_->session_infotainment_.isAuthenticated == false)
      {
        ESP_LOGW(TAG, "Not authenticated yet, try again in a few seconds");
        return_code = this->sendSessionInfoRequest(UniversalMessage_Domain_DOMAIN_INFOTAINMENT);
        if (return_code != 0)
        {
          ESP_LOGE(TAG, "Failed to send SessionInfoRequest");
          return return_code;
        }

        ESP_LOGI(TAG, "SessionInfoRequest sent to INFOTAINMENT");
      }

      // all good
      ESP_LOGD(TAG, "Infotainment preflight check passed");
      return 0;
    }

    int TeslaBLEVehicle::setChargingSwitch(bool isOn)
    {
      int return_code = this->infotainmentPreflightCheck();
      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed infotainment preflight check");
        return return_code;
      }

      ESP_LOGI(TAG, "Setting charging switch to %s", isOn ? "ON" : "OFF");
      unsigned char charge_message_buffer[tesla_ble_client_->MAX_BLE_MESSAGE_SIZE];
      size_t charge_message_length = 0;
      return_code = tesla_ble_client_->buildChargingSwitchMessage(isOn, charge_message_buffer, &charge_message_length);
      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed to build setChargingSwitch message");
        if (return_code == TeslaBLE::TeslaBLE_Status_E_ERROR_INVALID_SESSION)
        {
          tesla_ble_client_->session_infotainment_.setIsAuthenticated(false);
        }
        return return_code;
      }

      return_code = writeBLE(charge_message_buffer, charge_message_length, ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed to send charge message");
        return return_code;
      }
      return 0;
    }

    int TeslaBLEVehicle::setChargingAmps(int input_amps)
    {
      int return_code = this->infotainmentPreflightCheck();
      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed infotainment preflight check");
        return return_code;
      }

      // convert to uint32_t
      uint32_t amps = input_amps;

      ESP_LOGI(TAG, "Setting charge amps to %ld", amps);
      unsigned char charge_message_buffer[tesla_ble_client_->MAX_BLE_MESSAGE_SIZE];
      size_t charge_message_length;
      return_code = tesla_ble_client_->buildChargingAmpsMessage(amps, charge_message_buffer, &charge_message_length);
      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed to build setChargingAmps message");
        if (return_code == TeslaBLE::TeslaBLE_Status_E_ERROR_INVALID_SESSION)
        {
          tesla_ble_client_->session_infotainment_.setIsAuthenticated(false);
        }
        return return_code;
      }

      return_code = writeBLE(charge_message_buffer, charge_message_length, ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed to send charge amps message");
        return return_code;
      }
      return 0;
    }

    int TeslaBLEVehicle::setChargingLimit(int input_percent)
    {
      int return_code = this->infotainmentPreflightCheck();
      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed infotainment preflight check");
        return return_code;
      }

      // convert to uint32_t
      uint32_t percent = input_percent;

      ESP_LOGI(TAG, "Setting charge limit to %ld", percent);
      unsigned char charge_message_buffer[tesla_ble_client_->MAX_BLE_MESSAGE_SIZE];
      size_t charge_message_length;
      return_code = tesla_ble_client_->buildChargingSetLimitMessage(percent, charge_message_buffer, &charge_message_length);
      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed to build setChargingLimit message");
        if (return_code == TeslaBLE::TeslaBLE_Status_E_ERROR_INVALID_SESSION)
        {
          tesla_ble_client_->session_infotainment_.setIsAuthenticated(false);
        }
        return return_code;
      }

      return_code = writeBLE(charge_message_buffer, charge_message_length, ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed to send charge limit message");
        return return_code;
      }
      return 0;
    }

    int TeslaBLEVehicle::handleSessionInfoUpdate(UniversalMessage_RoutableMessage message, UniversalMessage_Domain domain)
    {
      UniversalMessage_RoutableMessage_session_info_t sessionInfo = message.payload.session_info;

      ESP_LOGD(TAG, "Received session info response from domain %s", domain_to_string(domain));
      TeslaBLE::Peer &session = domain == UniversalMessage_Domain_DOMAIN_INFOTAINMENT ? tesla_ble_client_->session_infotainment_ : tesla_ble_client_->session_vcsec_;

      // parse session info
      Signatures_SessionInfo session_info = Signatures_SessionInfo_init_default;
      int return_code = tesla_ble_client_->parsePayloadSessionInfo(&message.payload.session_info, &session_info);
      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed to parse session info response");
        return return_code;
      }
      log_session_info(TAG, &session_info);

      uint32_t generated_at = std::time(nullptr);
      uint32_t time_zero = generated_at - session_info.clock_time;

      ESP_LOGD(TAG, "Updating session info..");
      session.setCounter(&session_info.counter);
      session.setExpiresAt(&session_info.clock_time);
      session.setEpoch(session_info.epoch);
      session.setTimeZero(&time_zero);

      ESP_LOGD(TAG, "Loading %s public key from car..", domain_to_string(domain));
      // convert pb Failed to parse incoming message
      return_code = tesla_ble_client_->loadTeslaKey(true, session_info.publicKey.bytes, session_info.publicKey.size);

      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed load tesla %s key", domain_to_string(domain));
        return return_code;
      }

      ESP_LOGD(TAG, "Storing updated session info in NVS..");
      esp_err_t err = nvs_open("storage", NVS_READWRITE, &this->storage_handle_);
      if (err != ESP_OK)
      {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
        return 1;
      }
      // define nvs_key name based on domain
      const char *nvs_key_id = domain == UniversalMessage_Domain_DOMAIN_INFOTAINMENT ? nvs_key_id_infotainment : nvs_key_id_vcsec;
      err = nvs_set_blob(this->storage_handle_, nvs_key_id,
                         &session_info.publicKey.bytes,
                         session_info.publicKey.size);

      if (err != ESP_OK)
      {
        ESP_LOGE(TAG, "Failed to save tesla %s key: %s", domain_to_string(domain), esp_err_to_name(err));
        return 1;
      }
      err = nvs_commit(this->storage_handle_);
      if (err != ESP_OK)
      {
        ESP_LOGE(TAG, "Failed to commit %s key to storage: %s", domain_to_string(domain), esp_err_to_name(err));
        return 1;
      }
      const char *nvs_counter_id = domain == UniversalMessage_Domain_DOMAIN_INFOTAINMENT ? nvs_counter_id_infotainment : nvs_counter_id_vcsec;
      err = nvs_set_u32(this->storage_handle_, nvs_counter_id, session_info.counter);
      if (err != ESP_OK)
      {
        ESP_LOGE(TAG, "Failed to save %s counter: %s", domain_to_string(domain), esp_err_to_name(err));
        return 1;
      }
      return 0;
    }

    void TeslaBLEVehicle::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                              esp_ble_gattc_cb_param_t *param)
    {
      ESP_LOGV(TAG, "GATTC event %d", event);
      switch (event)
      {
      case ESP_GATTC_CONNECT_EVT:
      {
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
          ESP_LOGD(TAG, "Connection ID: %s", format_hex(connection_id, 16).c_str());
          tesla_ble_client_->setConnectionID(connection_id);
        }
        break;
      }

      case ESP_GATTC_SRVC_CHG_EVT:
      {
        esp_bd_addr_t bda;
        memcpy(bda, param->srvc_chg.remote_bda, sizeof(esp_bd_addr_t));
        ESP_LOGD(TAG, "ESP_GATTC_SRVC_CHG_EVT, bd_addr: %s", format_hex(bda, sizeof(esp_bd_addr_t)).c_str());
        break;
      }
      case ESP_GATTC_DISCONNECT_EVT:
      {
        this->handle_ = 0;
        this->read_handle_ = 0;
        this->write_handle_ = 0;

        tesla_ble_client_->session_vcsec_.setIsAuthenticated(false);
        tesla_ble_client_->session_infotainment_.setIsAuthenticated(false);
        // asleep sensor off
        this->updateAsleepState(NAN);
        // TODO: charging switch off

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

        ESP_LOGD(TAG, "Successfully set read and write char handle");
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
        ESP_LOGD(TAG, "ESP_GATTS_READ_EVT ");
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
        ESP_LOGD(TAG, "ESP_GATTC_READ_CHAR_EVT ");
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

        unsigned char private_key_buffer[228];
        size_t private_key_length = 0;
        int return_code = tesla_ble_client_->getPrivateKey(private_key_buffer, sizeof(private_key_buffer),
                                                           &private_key_length);
        if (return_code != 0)
        {
          ESP_LOGE(TAG, "Failed to get private key");
          break;
        }
        ESP_LOGD(TAG, "Loaded private key");

        unsigned char public_key_buffer[65];
        size_t public_key_length;
        return_code = tesla_ble_client_->getPublicKey(public_key_buffer, &public_key_length);
        if (return_code != 0)
        {
          ESP_LOGE(TAG, "Failed to get public key");
          break;
        }
        ESP_LOGD(TAG, "Loaded public key");

        return_code = this->vcsecPreflightCheck();
        if (return_code != 0)
        {
          ESP_LOGE(TAG, "Failed to send SessionInfoRequest");
          break;
        }
        ESP_LOGI(TAG, "SessionInfoRequest sent to VEHICLE_SECURITY");

        return_code = this->sendInfoStatus();
        if (return_code != 0)
        {
          ESP_LOGE(TAG, "Failed to send info status");
          break;
        }
        ESP_LOGI(TAG, "Sent initial info status request");

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
        ESP_LOGV(TAG, "Write char success");
        break;

      case ESP_GATTC_NOTIFY_EVT:
      {
        if (param->notify.conn_id != this->parent()->get_conn_id())
        {
          ESP_LOGW(TAG, "Received notify from unknown connection");
          break;
        }
        ESP_LOGV(TAG, "%d: - RAM left %ld", __LINE__, esp_get_free_heap_size());
        ESP_LOGV(TAG, "BLE RX chunk: %s", format_hex(param->notify.value, param->notify.value_len).c_str());

        UniversalMessage_RoutableMessage message = UniversalMessage_RoutableMessage_init_default;
        ESP_LOGV(TAG, "Receiving message in chunks");
        // append to buffer
        // Ensure the buffer has enough space
        if (this->current_size + param->notify.value_len > this->read_buffer.size())
        {
          ESP_LOGV(TAG, "Resizing read buffer");
          this->read_buffer.resize(this->current_size + param->notify.value_len);
        }

        // Append the new data
        std::memcpy(this->read_buffer.data() + this->current_size, param->notify.value, param->notify.value_len);
        this->current_size += param->notify.value_len;

        if (this->current_size >= 2)
        {
          int message_length = (this->read_buffer[0] << 8) | this->read_buffer[1];
          ESP_LOGV(TAG, "Message length: %d", message_length);
          if (message_length > tesla_ble_client_->MAX_BLE_MESSAGE_SIZE)
          {
            ESP_LOGW(TAG, "Message length (%d) exceeds max BLE message size", message_length);
            this->current_size = 0;
            this->read_buffer.clear();         // This will set the size to 0 and free unused memory
            this->read_buffer.shrink_to_fit(); // This will reduce the capacity to fit the size
            return;
          }

          // if len(c.inputBuffer) >= 2+msgLength {
          if (this->current_size >= 2 + message_length)
          {
            ESP_LOGD(TAG, "BLE RX: %s", format_hex(this->read_buffer.data(), this->current_size).c_str());
          }
          else
          {
            ESP_LOGD(TAG, "Buffered chunk, waiting for more data..");
            return;
          }
        }
        else
        {
          ESP_LOGD(TAG, "Not enough data to determine message length");
          return;
        }

        int return_code = tesla_ble_client_->parseUniversalMessageBLE(this->read_buffer.data(), this->current_size, &message);
        if (return_code != 0)
        {
          ESP_LOGW(TAG, "Failed to parse incoming message (maybe chunk?)");
          break;
        }
        ESP_LOGV(TAG, "Parsed UniversalMessage");
        // clear read buffer
        this->current_size = 0;
        this->read_buffer.clear();         // This will set the size to 0 and free unused memory
        this->read_buffer.shrink_to_fit(); // This will reduce the capacity to fit the size

        if (not message.has_from_destination)
        {
          ESP_LOGW(TAG, "[x] Dropping message with missing source");
          return;
        }
        UniversalMessage_Domain domain = message.from_destination.sub_destination.domain;

        if (message.request_uuid.size != 0 && message.request_uuid.size != 16)
        {
          ESP_LOGW(TAG, "[x] Dropping message with invalid request UUID length");
          return;
        }
        std::string request_uuid_hex_string = format_hex(message.request_uuid.bytes, message.request_uuid.size);
        const char *request_uuid_hex = request_uuid_hex_string.c_str();

        if (not message.has_to_destination)
        {
          ESP_LOGW(TAG, "[%s] Dropping message with missing destination", request_uuid_hex);
          return;
        }

        switch (message.to_destination.which_sub_destination)
        {
        case UniversalMessage_Destination_domain_tag:
        {
          ESP_LOGD(TAG, "[%s] Dropping message to %s", request_uuid_hex, domain_to_string(domain));
          return;
        }
        case UniversalMessage_Destination_routing_address_tag:
        {
          // Continue
          ESP_LOGD(TAG, "Continuing message with routing address");
          break;
        }
        default:
        {
          ESP_LOGW(TAG, "[%s] Dropping message with unrecognized destination type, %d", request_uuid_hex, message.to_destination.which_sub_destination);
          return;
        }
        }

        if (sizeof(message.to_destination.sub_destination.routing_address) != 16)
        {
          ESP_LOGW(TAG, "[%s] Dropping message with invalid address length", request_uuid_hex);
          return;
        }

        if (message.which_payload == UniversalMessage_RoutableMessage_session_info_tag)
        {
          return_code = this->handleSessionInfoUpdate(message, domain);
          if (return_code != 0)
          {
            ESP_LOGE(TAG, "Failed to handle session info update");
            return;
          }
          ESP_LOGI(TAG, "[%s] Updated session info for %s", request_uuid_hex, domain_to_string(domain));
          return;
        }

        // log error if present in message
        if (message.has_signedMessageStatus)
        {
          ESP_LOGD(TAG, "Received signed message status from domain %s", domain_to_string(domain));
          log_message_status(TAG, &message.signedMessageStatus);
          if (message.signedMessageStatus.operation_status == UniversalMessage_OperationStatus_E_OPERATIONSTATUS_ERROR)
          {
            ESP_LOGE(TAG, "Received error message from domain %s", domain_to_string(domain));
            // reset authentication for domain
            if (domain == UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY)
            {
              tesla_ble_client_->session_vcsec_.setIsAuthenticated(false);
            }
            else if (domain == UniversalMessage_Domain_DOMAIN_INFOTAINMENT)
            {
              tesla_ble_client_->session_infotainment_.setIsAuthenticated(false);
            }
            return;
          }
          else if (message.signedMessageStatus.operation_status ==
                   UniversalMessage_OperationStatus_E_OPERATIONSTATUS_WAIT)
          {
            ESP_LOGI(TAG, "Received wait message from domain %s", domain_to_string(domain));
            return;
          }
          else
          {
            ESP_LOGI(TAG, "Received success message from domain %s", domain_to_string(domain));
          }
          return;
        }

        log_routable_message(TAG, &message);
        switch (message.from_destination.which_sub_destination)
        {
        case UniversalMessage_Destination_domain_tag:
        {
          ESP_LOGI(TAG, "Received message from domain %s", domain_to_string(message.from_destination.sub_destination.domain));
          switch (message.from_destination.sub_destination.domain)
          {
          case UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY:
          {
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
              ESP_LOGD(TAG, "Received vehicle status");
              log_vehicle_status(TAG, &vcsec_message.sub_message.vehicleStatus);
              switch (vcsec_message.sub_message.vehicleStatus.vehicleSleepStatus)
              {
              case VCSEC_VehicleSleepStatus_E_VEHICLE_SLEEP_STATUS_AWAKE:
                this->updateAsleepState(false);
                break;
              case VCSEC_VehicleSleepStatus_E_VEHICLE_SLEEP_STATUS_ASLEEP:
                this->updateAsleepState(true);
                tesla_ble_client_->session_infotainment_.setIsAuthenticated(false);
                break;
              case VCSEC_VehicleSleepStatus_E_VEHICLE_SLEEP_STATUS_UNKNOWN:
              default:
                this->updateAsleepState(NAN);
                tesla_ble_client_->session_infotainment_.setIsAuthenticated(false);
                break;
              }
              break;
            }
            case VCSEC_FromVCSECMessage_commandStatus_tag:
            {
              ESP_LOGD(TAG, "Received command status");
              log_vcsec_command_status(TAG, &vcsec_message.sub_message.commandStatus);
              break;
            }
            case VCSEC_FromVCSECMessage_whitelistInfo_tag:
            {
              ESP_LOGD(TAG, "Received whitelist info");
              break;
            }
            case VCSEC_FromVCSECMessage_whitelistEntryInfo_tag:
            {
              ESP_LOGD(TAG, "Received whitelist entry info");
              break;
            }
            case VCSEC_FromVCSECMessage_nominalError_tag:
            {
              ESP_LOGE(TAG, "Received nominal error");
              ESP_LOGE(TAG, "  error: %s", generic_error_to_string(vcsec_message.sub_message.nominalError.genericError));
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
              ESP_LOGD(TAG, "InformationRequest public key: %s", format_hex(info_message.key.publicKey.bytes, info_message.key.publicKey.size).c_str());
              return;
            }
            break;
            }
            break;
          }

          case UniversalMessage_Domain_DOMAIN_INFOTAINMENT:
          {
            CarServer_Response carserver_response = CarServer_Response_init_default;
            int return_code = tesla_ble_client_->parsePayloadCarServerResponse(&message.payload.protobuf_message_as_bytes, &carserver_response);
            if (return_code != 0)
            {
              ESP_LOGE(TAG, "Failed to parse incoming message");
              return;
            }
            ESP_LOGD(TAG, "Parsed CarServer.Response");
            log_carserver_response(TAG, &carserver_response);
            break;
          }
          default:
          {
            ESP_LOGD(TAG, "Received message for %s", domain_to_string(message.to_destination.sub_destination.domain));
            ESP_LOGD(TAG, "Received message from unknown domain %s", domain_to_string(message.from_destination.sub_destination.domain));
            break;
          }
          break;
          }
          break;
        }

        case UniversalMessage_Destination_routing_address_tag:
        {
          ESP_LOGD(TAG, "Received message from routing address");
          break;
        }
        default:
        {
          ESP_LOGD(TAG, "Received message from unknown domain %s", domain_to_string(message.from_destination.sub_destination.domain));
          break;
        }
        break;
        }
        break;
      }

      default:
        ESP_LOGD(TAG, "Unhandled GATTC event %d", event);
        break;
      }
    }
  } // namespace tesla_ble_vehicle
} // namespace esphome
