#include <esp_random.h>
#include <esphome/core/helpers.h>
#include <esphome/core/log.h>
#include <nvs_flash.h>
#include <pb_decode.h>
#include <algorithm>
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
    void TeslaBLEVehicle::dump_config()
    {
      ESP_LOGCONFIG(TAG, "Tesla BLE Vehicle:");
      LOG_BINARY_SENSOR("  ", "Asleep Sensor", this->isAsleepSensor);
    }
    TeslaBLEVehicle::TeslaBLEVehicle() : tesla_ble_client_(new TeslaBLE::Client{})
    {
      ESP_LOGCONFIG(TAG, "Constructing Tesla BLE Vehicle component");
    }

    void TeslaBLEVehicle::setup()
    {
      ESP_LOGCONFIG(TAG, "Setting up TeslaBLEVehicle");
      this->service_uuid_ = espbt::ESPBTUUID::from_raw(SERVICE_UUID);
      this->read_uuid_ = espbt::ESPBTUUID::from_raw(READ_UUID);
      this->write_uuid_ = espbt::ESPBTUUID::from_raw(WRITE_UUID);

      this->initializeFlash();
      this->openNVSHandle();
      this->initializePrivateKey();
      this->loadSessionInfo();
    }

    void TeslaBLEVehicle::initializeFlash()
    {
      esp_err_t err = nvs_flash_init();
      if (err != ESP_OK)
      {
        ESP_LOGE(TAG, "Failed to initialize flash: %s", esp_err_to_name(err));
        esp_restart();
      }
    }

    void TeslaBLEVehicle::openNVSHandle()
    {
      esp_err_t err = nvs_open("storage", NVS_READWRITE, &this->storage_handle_);
      if (err != ESP_OK)
      {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
        esp_restart();
      }
    }

    void TeslaBLEVehicle::initializePrivateKey()
    {
      if (nvs_initialize_private_key() != 0)
      {
        ESP_LOGE(TAG, "Failed to initialize private key");
        esp_restart();
      }
    }

    void TeslaBLEVehicle::loadSessionInfo()
    {
      loadDomainSessionInfo(UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY);
      loadDomainSessionInfo(UniversalMessage_Domain_DOMAIN_INFOTAINMENT);
    }

    void TeslaBLEVehicle::loadDomainSessionInfo(UniversalMessage_Domain domain)
    {
      ESP_LOGCONFIG(TAG, "Loading %s session info from NVS..", domain_to_string(domain));
      Signatures_SessionInfo session_info = Signatures_SessionInfo_init_default;
      if (nvs_load_session_info(&session_info, domain) != 0)
      {
        ESP_LOGW(TAG, "Failed to load %s session info from NVS", domain_to_string(domain));
      }
    }

    void TeslaBLEVehicle::process_command_queue()
    {
      if (command_queue_.empty())
      {
        return;
      }

      BLECommand &current_command = command_queue_.front();
      uint32_t now = millis();

      // Overall timeout check
      if (now - current_command.started_at > COMMAND_TIMEOUT)
      {
        ESP_LOGE(TAG, "[%s] Command timed out after %d ms",
                 current_command.execute_name.c_str(), COMMAND_TIMEOUT);
        command_queue_.pop();
        return;
      }

      switch (current_command.state)
      {
      case BLECommandState::IDLE:
        ESP_LOGI(TAG, "[%s] Preparing command..", current_command.execute_name.c_str());
        current_command.started_at = now;
        switch (current_command.domain)
        {
        case UniversalMessage_Domain_DOMAIN_BROADCAST:
          ESP_LOGD(TAG, "[%s] No auth required, executing command..", current_command.execute_name.c_str());
          current_command.state = BLECommandState::READY;
          break;
        case UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY:
          ESP_LOGD(TAG, "[%s] VCSEC required, validating VCSEC session..", current_command.execute_name.c_str());
          current_command.state = BLECommandState::WAITING_FOR_VCSEC_AUTH;
          break;
        case UniversalMessage_Domain_DOMAIN_INFOTAINMENT:
          ESP_LOGD(TAG, "[%s] INFOTAINMENT required, validating INFOTAINMENT session..", current_command.execute_name.c_str());
          current_command.state = BLECommandState::WAITING_FOR_INFOTAINMENT_AUTH;
          break;
        }
        break;
      case BLECommandState::WAITING_FOR_VCSEC_AUTH:
        if (now - current_command.last_tx_at > MAX_LATENCY)
        {
          auto session = tesla_ble_client_->getPeer(UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY);
          if (session->isInitialized())
          {
            ESP_LOGD(TAG, "[%s] VCSEC session authenticated", current_command.execute_name.c_str());
            switch (current_command.domain)
            {
            case UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY:
              current_command.state = BLECommandState::READY;
              break;
            case UniversalMessage_Domain_DOMAIN_INFOTAINMENT:
              ESP_LOGD(TAG, "[%s] Validating INFOTAINMENT session..", current_command.execute_name.c_str());
              current_command.state = BLECommandState::WAITING_FOR_INFOTAINMENT_AUTH;
              break;
            case UniversalMessage_Domain_DOMAIN_BROADCAST:
              ESP_LOGE(TAG, "[%s] Invalid state: VCSEC authenticated but no auth required", current_command.execute_name.c_str());
              // pop command
              command_queue_.pop();
              break;
            }
            break;
          }
          else
          {
            ESP_LOGW(TAG, "[%s] VCSEC auth expired, refreshing session..", current_command.execute_name.c_str());
            current_command.retry_count++;
            ESP_LOGD(TAG, "[%s] Waiting for VCSEC auth | attempt %d/%d",
                     current_command.execute_name.c_str(), current_command.retry_count, MAX_RETRIES);
            if (current_command.retry_count <= MAX_RETRIES)
            {
              sendSessionInfoRequest(UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY);
              sendSessionInfoRequest(UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY);
              current_command.last_tx_at = now;
              current_command.state = BLECommandState::WAITING_FOR_VCSEC_AUTH_RESPONSE;
            }
            else
            {
              ESP_LOGE(TAG, "[%s] Failed to authenticate VCSEC after %d retries, giving up", current_command.execute_name.c_str(), MAX_RETRIES);
              // pop command
              command_queue_.pop();
            }
          }
        }
        break;

      case BLECommandState::WAITING_FOR_VCSEC_AUTH_RESPONSE:
        if (now - current_command.last_tx_at > MAX_LATENCY)
        {
          ESP_LOGW(TAG, "[%s] Timeout while waiting for VCSEC SessionInfo, retrying..",
                   current_command.execute_name.c_str());
          current_command.state = BLECommandState::WAITING_FOR_VCSEC_AUTH;
        }
        break;

      case BLECommandState::WAITING_FOR_INFOTAINMENT_AUTH:
        if (now - current_command.last_tx_at > MAX_LATENCY)
        {
          if (!this->isAsleepSensor->state == false)
          {
            ESP_LOGW(TAG, "[%s] Car is asleep, initiating wake..", current_command.execute_name.c_str());
            current_command.state = BLECommandState::WAITING_FOR_WAKE;
          }
          else
          {
            auto session = tesla_ble_client_->getPeer(UniversalMessage_Domain_DOMAIN_INFOTAINMENT);
            if (session->isInitialized())
            {
              ESP_LOGD(TAG, "[%s] INFOTAINMENT authenticated", current_command.execute_name.c_str());
              current_command.state = BLECommandState::READY;
            }
            else
            {
              ESP_LOGW(TAG, "[%s] INFOTAINMENT auth expired, refreshing session..", current_command.execute_name.c_str());
              current_command.retry_count++;
              ESP_LOGD(TAG, "[%s] Waiting for INFOTAINMENT auth.. | attempt %d/%d",
                       current_command.execute_name.c_str(), current_command.retry_count, MAX_RETRIES);
              if (current_command.retry_count <= MAX_RETRIES)
              {
                sendSessionInfoRequest(UniversalMessage_Domain_DOMAIN_INFOTAINMENT);
                sendSessionInfoRequest(UniversalMessage_Domain_DOMAIN_INFOTAINMENT);
                current_command.last_tx_at = now;
                current_command.state = BLECommandState::WAITING_FOR_INFOTAINMENT_AUTH_RESPONSE;
              }
              else
              {
                ESP_LOGE(TAG, "[%s] Failed INFOTAINMENT auth after %d retries, giving up",
                         current_command.execute_name.c_str(), MAX_RETRIES);
                // pop command
                command_queue_.pop();
              }
            }
          }
        }
        break;

      case BLECommandState::WAITING_FOR_WAKE:
        if (now - current_command.last_tx_at > MAX_LATENCY)
        {
          if (current_command.retry_count > MAX_RETRIES)
          {
            ESP_LOGE(TAG, "[%s] Failed to wake vehicle after %d retries",
                     current_command.execute_name.c_str(), MAX_RETRIES);
            // pop command
            command_queue_.pop();
          }
          else
          {
            ESP_LOGD(TAG, "[%s] Sending wake command | attempt %d/%d",
                     current_command.execute_name.c_str(), current_command.retry_count, MAX_RETRIES);
            int result = this->sendVCSECActionMessage(VCSEC_RKEAction_E_RKE_ACTION_WAKE_VEHICLE);
            if (result != 0)
            {
              ESP_LOGE(TAG, "[%s] Failed to send wake command", current_command.execute_name.c_str());
            }
            current_command.last_tx_at = now;
            current_command.retry_count++;
            current_command.state = BLECommandState::WAITING_FOR_WAKE_RESPONSE;
          }
        }
        break;

      case BLECommandState::WAITING_FOR_WAKE_RESPONSE:
        if (now - current_command.last_tx_at > MAX_LATENCY)
        {
          if (this->isAsleepSensor->state == false)
          {
            if (strcmp(current_command.execute_name.c_str(), "wake vehicle") == 0) {
              ESP_LOGI(TAG, "[%s] Vehicle is awake, command completed", current_command.execute_name.c_str());
              command_queue_.pop();
            }
            else {
              ESP_LOGI(TAG, "[%s] Vehicle is awake, waiting for infotainment auth", current_command.execute_name.c_str());
              current_command.state = BLECommandState::WAITING_FOR_INFOTAINMENT_AUTH;
              current_command.retry_count = 0;
            }
          }
          else
          {
            // send info status
            ESP_LOGD(TAG, "[%s] Polling for wake response.. | attempt %d/%d",
                     current_command.execute_name.c_str(),
                     current_command.retry_count, MAX_RETRIES);

            // alternate between sending wake command and info status
            // vehicle can need multiple wake commands to wake up
            if (current_command.retry_count % 2 == 0)
            {
              int result = this->sendVCSECActionMessage(VCSEC_RKEAction_E_RKE_ACTION_WAKE_VEHICLE);
              if (result != 0)
              {
                ESP_LOGE(TAG, "[%s] Failed to send wake command", current_command.execute_name.c_str());
              }
            }
            else
            {
              int result = this->sendVCSECInformationRequest();
              if (result != 0)
              {
                ESP_LOGE(TAG, "[%s] Failed to send VCSECInformationRequest", current_command.execute_name.c_str());
              }
            }
            current_command.last_tx_at = now;
            current_command.retry_count++;

            if (current_command.retry_count > MAX_RETRIES)
            {
              ESP_LOGE(TAG, "[%s] Failed to wake up vehicle after %d retries",
                       current_command.execute_name.c_str(),
                       MAX_RETRIES);
              // pop command
              command_queue_.pop();
            }
          }
        }
        break;

      case BLECommandState::WAITING_FOR_INFOTAINMENT_AUTH_RESPONSE:
        if (now - current_command.last_tx_at > MAX_LATENCY)
        {
          ESP_LOGW(TAG, "[%s] Timeout while waiting for INFOTAINMENT SessionInfo, retrying..", current_command.execute_name.c_str());
          current_command.state = BLECommandState::WAITING_FOR_INFOTAINMENT_AUTH;
          current_command.retry_count++;
        }
        break;

      case BLECommandState::READY:
        // Ready to send a command
        if (now - current_command.last_tx_at > MAX_LATENCY)
        {
          current_command.retry_count++;
          if (current_command.retry_count > MAX_RETRIES)
          {
            ESP_LOGE(TAG, "[%s] Failed to execute command after %d retries, giving up",
                     current_command.execute_name.c_str(),
                     MAX_RETRIES);
            command_queue_.pop();
          }
          else
          {
            ESP_LOGI(TAG, "[%s] Executing command.. | attempt %d/%d",
                     current_command.execute_name.c_str(),
                     current_command.retry_count, MAX_RETRIES);
            int result = current_command.execute();
            if (result == 0)
            {
              ESP_LOGI(TAG, "[%s] Command executed, waiting for response..",
                       current_command.execute_name.c_str());
              current_command.last_tx_at = now;

              if (strcmp(current_command.execute_name.c_str(), "wake vehicle") == 0)
              {
                current_command.state = BLECommandState::WAITING_FOR_WAKE_RESPONSE;
              }
              else
              {
                current_command.state = BLECommandState::WAITING_FOR_RESPONSE;
              }
            }
            else
            {
              ESP_LOGE(TAG, "[%s] Command execution failed, retrying..",
                       current_command.execute_name.c_str());
            }
          }
        }
        break;
      case BLECommandState::WAITING_FOR_RESPONSE:
        if (now - current_command.last_tx_at > MAX_LATENCY)
        {
          ESP_LOGW(TAG, "[%s] Timed out while waiting for command response",
                   current_command.execute_name.c_str());
          current_command.state = BLECommandState::READY;
        }
        break;
      }
    }

    void TeslaBLEVehicle::process_ble_write_queue()
    {
      if (this->ble_write_queue_.empty())
      {
        return;
      }

      BLETXChunk chunk = this->ble_write_queue_.front();
      int gattc_if = this->parent()->get_gattc_if();
      uint16_t conn_id = this->parent()->get_conn_id();
      esp_err_t err = esp_ble_gattc_write_char(gattc_if, conn_id, this->write_handle_, chunk.data.size(), chunk.data.data(), chunk.write_type, chunk.auth_req);
      if (err)
      {
        ESP_LOGW(TAG, "Error sending write value to BLE gattc server, error=%d", err);
      }
      else
      {
        ESP_LOGV(TAG, "BLE TX: %s", format_hex(chunk.data.data(), chunk.data.size()).c_str());
        this->ble_write_queue_.pop();
      }
    }

    void TeslaBLEVehicle::process_ble_read_queue()
    {
      if (this->ble_read_queue_.empty())
      {
        return;
      }

      ESP_LOGV(TAG, "Processing BLE read queue..");
      BLERXChunk read_chunk_ = this->ble_read_queue_.front();
      ESP_LOGV(TAG, "BLE RX chunk: %s", format_hex(read_chunk_.buffer.data(), read_chunk_.buffer.size()).c_str());

      // check we are not overflowing the buffer before appending data
      size_t buffer_len_post_append = read_chunk_.buffer.size() + this->ble_read_buffer_.size();
      if (buffer_len_post_append > MAX_BLE_MESSAGE_SIZE)
      {
        ESP_LOGE(TAG, "BLE RX: Message length (%d) exceeds max BLE message size", buffer_len_post_append);
        // clear buffer
        this->ble_read_buffer_.clear();
        this->ble_read_buffer_.shrink_to_fit();
        return;
      }

      // Append the new data
      ESP_LOGV(TAG, "BLE RX: Appending new data to read buffer");
      this->ble_read_buffer_.insert(this->ble_read_buffer_.end(), read_chunk_.buffer.begin(), read_chunk_.buffer.end());
      this->ble_read_queue_.pop();

      if (this->ble_read_buffer_.size() >= 2)
      {
        int message_length = (this->ble_read_buffer_[0] << 8) | this->ble_read_buffer_[1];

        if (this->ble_read_buffer_.size() >= 2 + message_length)
        {
          ESP_LOGD(TAG, "BLE RX: %s", format_hex(this->ble_read_buffer_.data(), this->ble_read_buffer_.size()).c_str());
        }
        else
        {
          ESP_LOGD(TAG, "BLE RX: Buffered chunk, waiting for more data.. (%d/%d)", this->ble_read_buffer_.size(), 2 + message_length);
          return;
        }
      }
      else
      {
        ESP_LOGD(TAG, "BLE RX: Not enough data to determine message length");
        return;
      }

      UniversalMessage_RoutableMessage message = UniversalMessage_RoutableMessage_init_default;
      int return_code = tesla_ble_client_->parseUniversalMessageBLE(
          this->ble_read_buffer_.data(), this->ble_read_buffer_.size(), &message);
      if (return_code != 0)
      {
        ESP_LOGW(TAG, "BLE RX: Failed to parse incoming message");
      }
      ESP_LOGD(TAG, "BLE RX: Parsed UniversalMessage");
      // clear read buffer
      this->ble_read_buffer_.clear();         // This will set the size to 0 and free unused memory
      this->ble_read_buffer_.shrink_to_fit(); // This will reduce the capacity to fit the size

      response_queue_.emplace(message);
      return;
    }
    void TeslaBLEVehicle::process_response_queue()
    {
      if (response_queue_.empty())
      {
        return;
      }

      BLEResponse response = response_queue_.front();
      UniversalMessage_RoutableMessage message = response.message;
      response_queue_.pop();

      if (not message.has_from_destination)
      {
        ESP_LOGD(TAG, "[x] Dropping message with missing source");
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

      if (message.to_destination.sub_destination.routing_address.size != 16)
      {
        ESP_LOGW(TAG, "[%s] Dropping message with invalid address length", request_uuid_hex);
        return;
      }

      if (message.has_signedMessageStatus && message.signedMessageStatus.operation_status == UniversalMessage_OperationStatus_E_OPERATIONSTATUS_ERROR)
      {
        // reset authentication for domain
        auto session = tesla_ble_client_->getPeer(domain);
        invalidateSession(domain);
      }

      if (message.which_payload == UniversalMessage_RoutableMessage_session_info_tag)
      {
        int return_code = this->handleSessionInfoUpdate(message, domain);
        if (return_code != 0)
        {
          ESP_LOGE(TAG, "Failed to handle session info update");
          return;
        }
        ESP_LOGI(TAG, "[%s] Updated session info for %s", request_uuid_hex, domain_to_string(domain));
      }

      if (message.has_signedMessageStatus)
      {
        ESP_LOGD(TAG, "Received signed message status from domain %s", domain_to_string(domain));
        log_message_status(TAG, &message.signedMessageStatus);
        if (message.signedMessageStatus.operation_status == UniversalMessage_OperationStatus_E_OPERATIONSTATUS_ERROR)
        {
          ESP_LOGE(TAG, "Received error message from domain %s", domain_to_string(domain));
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

      if (message.which_payload == UniversalMessage_RoutableMessage_session_info_tag)
      {
        // log error and return if session info is present
        return;
      }

      log_routable_message(TAG, &message);
      switch (message.from_destination.which_sub_destination)
      {
      case UniversalMessage_Destination_domain_tag:
      {
        ESP_LOGD(TAG, "Received message from domain %s", domain_to_string(message.from_destination.sub_destination.domain));
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
            handleVCSECVehicleStatus(vcsec_message.sub_message.vehicleStatus);

            if (!command_queue_.empty())
            {
              BLECommand &current_command = command_queue_.front();
              switch (current_command.domain)
              {
              case UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY:
                if (current_command.state == BLECommandState::WAITING_FOR_RESPONSE)
                {
                  ESP_LOGI(TAG, "[%s] Received vehicle status, command completed", current_command.execute_name.c_str());
                  command_queue_.pop();
                }
                break;
              case UniversalMessage_Domain_DOMAIN_INFOTAINMENT:
                switch (current_command.state)
                {
                case BLECommandState::WAITING_FOR_WAKE:
                case BLECommandState::WAITING_FOR_WAKE_RESPONSE:
                  switch (vcsec_message.sub_message.vehicleStatus.vehicleSleepStatus)
                  {
                  case VCSEC_VehicleSleepStatus_E_VEHICLE_SLEEP_STATUS_AWAKE:
                    if (strcmp(current_command.execute_name.c_str(), "wake vehicle") == 0)
                    {
                      ESP_LOGI(TAG, "[%s] Received vehicle status, command completed",
                               current_command.execute_name.c_str());
                      command_queue_.pop();
                    }
                    else
                    {
                      ESP_LOGI(TAG, "[%s] Received vehicle status, vehicle is awake",
                               current_command.execute_name.c_str());
                      current_command.state = BLECommandState::WAITING_FOR_INFOTAINMENT_AUTH;
                      current_command.retry_count = 0;
                    }
                    break;
                  default:
                    ESP_LOGD(TAG, "[%s] Received vehicle status, vehicle is not awake",
                             current_command.execute_name.c_str());
                    break;
                  }
                  break;

                case BLECommandState::WAITING_FOR_RESPONSE:
                  if (strcmp(current_command.execute_name.c_str(), "wake vehicle") == 0 ||
                      strcmp(current_command.execute_name.c_str(), "data update") == 0)
                  {
                    ESP_LOGI(TAG, "[%s] Received vehicle status, command completed",
                             current_command.execute_name.c_str());
                    command_queue_.pop();
                  }
                  else if (strcmp(current_command.execute_name.c_str(), "data update | forced") == 0)
                  {
                    switch (vcsec_message.sub_message.vehicleStatus.vehicleSleepStatus)
                    {
                    case VCSEC_VehicleSleepStatus_E_VEHICLE_SLEEP_STATUS_AWAKE:
                      ESP_LOGI(TAG, "[%s] Received vehicle status, command completed",
                               current_command.execute_name.c_str());
                      command_queue_.pop();
                      break;
                    default:
                      ESP_LOGD(TAG, "[%s] Received vehicle status, infotainment is not awake",
                               current_command.execute_name.c_str());
                      invalidateSession(UniversalMessage_Domain_DOMAIN_INFOTAINMENT);
                      current_command.state = BLECommandState::WAITING_FOR_INFOTAINMENT_AUTH;
                    }
                  }
                  break;
                default:
                  break;
                }
              default:
                break;
              }
            }
            break;
          }
          case VCSEC_FromVCSECMessage_commandStatus_tag:
          {
            ESP_LOGD(TAG, "Received VCSEC command status");
            log_vcsec_command_status(TAG, &vcsec_message.sub_message.commandStatus);
            if (!command_queue_.empty())
            {
              BLECommand &current_command = command_queue_.front();
              if (current_command.domain == UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY)
              {
                switch (vcsec_message.sub_message.commandStatus.operationStatus)
                {
                case VCSEC_OperationStatus_E_OPERATIONSTATUS_OK:
                  if (current_command.state == BLECommandState::WAITING_FOR_RESPONSE)
                  {
                    ESP_LOGI(TAG, "[%s] Received VCSEC OK message, command completed",
                             current_command.execute_name.c_str());
                    command_queue_.pop();
                  }
                  break;
                case VCSEC_OperationStatus_E_OPERATIONSTATUS_WAIT:
                  if (current_command.state == BLECommandState::WAITING_FOR_RESPONSE)
                  {
                    ESP_LOGW(TAG, "[%s] Received VCSEC WAIT message, requeuing command..",
                             current_command.execute_name.c_str());
                    current_command.last_tx_at = millis();
                    current_command.state = BLECommandState::READY;
                  }
                  break;
                case VCSEC_OperationStatus_E_OPERATIONSTATUS_ERROR:
                  ESP_LOGW(TAG, "[%s] Received VCSEC ERROR message, retrying command..",
                           current_command.execute_name.c_str());
                  if (current_command.state == BLECommandState::WAITING_FOR_RESPONSE)
                  {
                    current_command.state = BLECommandState::READY;
                  }
                  break;
                }
              }
            }
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
          if (carserver_response.has_actionStatus && !command_queue_.empty())
          {
            BLECommand &current_command = command_queue_.front();
            if (current_command.domain == UniversalMessage_Domain_DOMAIN_INFOTAINMENT)
            {
              switch (carserver_response.actionStatus.result)
              {
              case CarServer_OperationStatus_E_OPERATIONSTATUS_OK:
                if (current_command.state == BLECommandState::WAITING_FOR_RESPONSE)
                {
                  ESP_LOGI(TAG, "[%s] Received CarServer OK message, command completed", current_command.execute_name.c_str());
                  command_queue_.pop();
                }
                break;
              case CarServer_OperationStatus_E_OPERATIONSTATUS_ERROR:
                // if charging switch is turned on and reason = "is_charging" it's OK
                // if charging switch is turned of and reason = "is_not_charging" it's OK
                if (carserver_response.actionStatus.has_result_reason)
                {
                  switch (carserver_response.actionStatus.result_reason.which_reason)
                  {
                  case CarServer_ResultReason_plain_text_tag:
                    if (strcmp(carserver_response.actionStatus.result_reason.reason.plain_text, "is_charging") == 0 || strcmp(carserver_response.actionStatus.result_reason.reason.plain_text, "is_not_charging") == 0)
                    {
                      ESP_LOGD(TAG, "[%s] Received charging status: %s",
                               current_command.execute_name.c_str(),
                               carserver_response.actionStatus.result_reason.reason.plain_text);
                      if (current_command.state == BLECommandState::WAITING_FOR_RESPONSE)
                      {
                        ESP_LOGI(TAG, "[%s] Received CarServer OK message, command completed",
                                 current_command.execute_name.c_str());
                        command_queue_.pop();
                      }
                    }
                    break;
                  default:
                    break;
                  }
                }
                else
                {
                  ESP_LOGE(TAG, "[%s] Received CarServer ERROR message, retrying command..",
                           current_command.execute_name.c_str());
                  if (current_command.state == BLECommandState::WAITING_FOR_RESPONSE)
                  {
                    current_command.state = BLECommandState::READY;
                  }
                }
                break;
              }
            }
          }
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
    }

    void TeslaBLEVehicle::loop()
    {
      if (this->node_state != espbt::ClientState::ESTABLISHED)
      {
        if (!command_queue_.empty())
        {
          // clear command queue if not connected or on first boot (prevent restore value triggering commands)
          command_queue_.pop();
        }
        return;
      }

      process_ble_read_queue();
      process_response_queue();
      process_command_queue();
      process_ble_write_queue();
    }

    void TeslaBLEVehicle::update()
    {
      ESP_LOGD(TAG, "Updating Tesla BLE Vehicle component..");
      if (this->node_state == espbt::ClientState::ESTABLISHED)
      {
        ESP_LOGD(TAG, "Querying vehicle status update..");
        enqueueVCSECInformationRequest();
        return;
      }
    }

    int TeslaBLEVehicle::nvs_save_session_info(const Signatures_SessionInfo &session_info, const UniversalMessage_Domain domain)
    {
      ESP_LOGD(TAG, "Storing updated session info in NVS for domain %s", domain_to_string(domain));
      const char *nvs_key = (domain == UniversalMessage_Domain_DOMAIN_INFOTAINMENT) ? nvs_key_infotainment : nvs_key_vcsec;

      // Estimate required buffer size
      size_t session_info_encode_buffer_size = Signatures_SessionInfo_size + 10; // Add some padding
      std::vector<pb_byte_t> session_info_encode_buffer(session_info_encode_buffer_size);

      // Encode session info into protobuf message
      int return_code = TeslaBLE::pb_encode_fields(session_info_encode_buffer.data(), &session_info_encode_buffer_size, Signatures_SessionInfo_fields, &session_info);
      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed to encode session info for domain %s. Error code: %d", domain_to_string(domain), return_code);
        return return_code;
      }
      ESP_LOGD(TAG, "Session info encoded to %d bytes for domain %s", session_info_encode_buffer_size, domain_to_string(domain));
      ESP_LOGD(TAG, "Session info: %s", format_hex(session_info_encode_buffer.data(), session_info_encode_buffer_size).c_str());

      // Store encoded session info in NVS
      esp_err_t err = nvs_set_blob(this->storage_handle_, nvs_key, session_info_encode_buffer.data(), session_info_encode_buffer_size);
      if (err != ESP_OK)
      {
        ESP_LOGE(TAG, "Failed to set %s key in storage: %s", domain_to_string(domain), esp_err_to_name(err));
        return static_cast<int>(err);
      }

      // Commit the changes to NVS
      err = nvs_commit(this->storage_handle_);
      if (err != ESP_OK)
      {
        ESP_LOGE(TAG, "Failed to commit storage for domain %s: %s", domain_to_string(domain), esp_err_to_name(err));
        return static_cast<int>(err);
      }

      ESP_LOGD(TAG, "Successfully saved session info for domain %s", domain_to_string(domain));
      return 0;
    }

    int TeslaBLEVehicle::nvs_load_session_info(Signatures_SessionInfo *session_info, const UniversalMessage_Domain domain)
    {
      if (session_info == nullptr)
      {
        ESP_LOGE(TAG, "Invalid session_info pointer");
        return 1;
      }

      const std::string nvs_key = (domain == UniversalMessage_Domain_DOMAIN_INFOTAINMENT) ? nvs_key_infotainment : nvs_key_vcsec;

      size_t required_session_info_size = 0;
      esp_err_t err = nvs_get_blob(this->storage_handle_, nvs_key.c_str(), nullptr, &required_session_info_size);
      if (err != ESP_OK)
      {
        ESP_LOGE(TAG, "Failed to read %s key size from storage: %s", domain_to_string(domain), esp_err_to_name(err));
        return 1;
      }

      std::vector<uint8_t> session_info_protobuf(required_session_info_size);
      err = nvs_get_blob(this->storage_handle_, nvs_key.c_str(), session_info_protobuf.data(), &required_session_info_size);
      if (err != ESP_OK)
      {
        ESP_LOGE(TAG, "Failed to read %s key data from storage: %s", domain_to_string(domain), esp_err_to_name(err));
        return 1;
      }

      ESP_LOGI(TAG, "Loaded %s session info from NVS", domain_to_string(domain));
      ESP_LOGD(TAG, "Session info: %s", format_hex(session_info_protobuf.data(), required_session_info_size).c_str());

      pb_istream_t stream = pb_istream_from_buffer(session_info_protobuf.data(), required_session_info_size);
      if (!pb_decode(&stream, Signatures_SessionInfo_fields, session_info))
      {
        ESP_LOGE(TAG, "Failed to decode session info response: %s", PB_GET_ERROR(&stream));
        return 1;
      }

      log_session_info(TAG, session_info);

      auto session = tesla_ble_client_->getPeer(domain);
      session->updateSession(session_info);

      return 0;
    }

    int TeslaBLEVehicle::nvs_initialize_private_key()
    {
      size_t required_private_key_size = 0;
      int err = nvs_get_blob(this->storage_handle_, "private_key", NULL,
                             &required_private_key_size);

      if (err != ESP_OK)
      {
        ESP_LOGW(TAG, "Failed read private key from storage: %s",
                 esp_err_to_name(err));
      }

      if (required_private_key_size == 0)
      {
        int result_code = tesla_ble_client_->createPrivateKey();
        if (result_code != 0)
        {
          ESP_LOGE(TAG, "Failed create private key");
          return result_code;
        }

        unsigned char private_key_buffer[PRIVATE_KEY_SIZE];
        size_t private_key_length = 0;
        tesla_ble_client_->getPrivateKey(
            private_key_buffer, sizeof(private_key_buffer),
            &private_key_length);

        esp_err_t err = nvs_set_blob(
            this->storage_handle_, "private_key",
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
        err = nvs_get_blob(
            this->storage_handle_, "private_key",
            private_key_buffer, &required_private_key_size);
        if (err != ESP_OK)
        {
          ESP_LOGE(TAG, "Failed read private key from storage: %s",
                   esp_err_to_name(err));
          return 1;
        }

        int result_code = tesla_ble_client_->loadPrivateKey(
            private_key_buffer, required_private_key_size);
        if (result_code != 0)
        {
          ESP_LOGE(TAG, "Failed load private key");
          return result_code;
        }

        ESP_LOGI(TAG, "Private key loaded successfully");
      }
      return 0;
    }

    void TeslaBLEVehicle::set_vin(const char *vin)
    {
      tesla_ble_client_->setVIN(vin);
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

      unsigned char private_key_buffer[PRIVATE_KEY_SIZE];
      size_t private_key_length = 0;
      tesla_ble_client_->getPrivateKey(private_key_buffer, sizeof(private_key_buffer),
                                       &private_key_length);

      esp_err_t err = nvs_flash_init();
      if (err != ESP_OK)
      {
        ESP_LOGE(TAG, "Failed to initialize flash: %s", esp_err_to_name(err));
      }

      err = nvs_open("storage", NVS_READWRITE, &this->storage_handle_);
      if (err != ESP_OK)
      {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
      }

      err = nvs_set_blob(this->storage_handle_, "private_key",
                         private_key_buffer, private_key_length);
      if (err != ESP_OK)
      {
        ESP_LOGE(TAG, "Failed commit storage: %s", esp_err_to_name(err));
      }

      err = nvs_commit(this->storage_handle_);
      if (err != ESP_OK)
      {
        ESP_LOGE(TAG, "Failed commit storage: %s", esp_err_to_name(err));
      }

      ESP_LOGI(TAG, "Private key successfully created");
    }

    int TeslaBLEVehicle::startPair()
    {
      ESP_LOGI(TAG, "Starting pairing");
      ESP_LOGI(TAG, "Not authenticated yet, building whitelist message");
      unsigned char whitelist_message_buffer[VCSEC_ToVCSECMessage_size];
      size_t whitelist_message_length = 0;
      // support for wake command will be added to ROLE_CHARGING_MANAGER in a future vehicle firmware update
      // https://github.com/teslamotors/vehicle-command/issues/232#issuecomment-2181503570
      // TODO: change back to ROLE_CHARGING_MANAGER when it's supported
      int return_code = tesla_ble_client_->buildWhiteListMessage(Keys_Role_ROLE_DRIVER, VCSEC_KeyFormFactor_KEY_FORM_FACTOR_CLOUD_KEY, whitelist_message_buffer, &whitelist_message_length);
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
      unsigned char message_buffer[UniversalMessage_RoutableMessage_size];
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

    int TeslaBLEVehicle::writeBLE(
        const unsigned char *message_buffer, size_t message_length,
        esp_gatt_write_type_t write_type, esp_gatt_auth_req_t auth_req)
    {
      ESP_LOGD(TAG, "BLE TX: %s", format_hex(message_buffer, message_length).c_str());
      // BLE MTU is 23 bytes, so we need to split the message into chunks (20 bytes as in vehicle_command)
      for (size_t i = 0; i < message_length; i += BLOCK_LENGTH)
      {
        size_t chunkLength = std::min(static_cast<size_t>(BLOCK_LENGTH), message_length - i);
        std::vector<unsigned char> chunk(message_buffer + i, message_buffer + i + chunkLength);

        // add to write queue
        this->ble_write_queue_.emplace(chunk, write_type, auth_req);
      }
      ESP_LOGD(TAG, "BLE TX: Added to write queue.");
      return 0;
    }

    int TeslaBLEVehicle::sendVCSECActionMessage(VCSEC_RKEAction_E action)
    {
      ESP_LOGD(TAG, "Building sendVCSECActionMessage");
      unsigned char action_message_buffer[UniversalMessage_RoutableMessage_size];
      size_t action_message_buffer_length = 0;
      int return_code = tesla_ble_client_->buildVCSECActionMessage(action, action_message_buffer, &action_message_buffer_length);
      if (return_code != 0)
      {
        if (return_code == TeslaBLE::TeslaBLE_Status_E_ERROR_INVALID_SESSION)
        {
          auto session = tesla_ble_client_->getPeer(UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY);
          invalidateSession(UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY);
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
      if (this->isAsleepSensor->state == false)
      {
        ESP_LOGI(TAG, "Vehicle is already awake");
        return 0;
      }

      // enqueue command
      ESP_LOGI(TAG, "Adding wakeVehicle command to queue");
      command_queue_.emplace(
          UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY, [this]()
          {
        int return_code = this->sendVCSECActionMessage(VCSEC_RKEAction_E_RKE_ACTION_WAKE_VEHICLE);
        if (return_code != 0)
        {
          ESP_LOGE(TAG, "Failed to send wake command");
          return return_code;
        }
        return 0; },
          "wake vehicle");

      return 0;
    }

    int TeslaBLEVehicle::sendVCSECInformationRequest()
    {
      ESP_LOGD(TAG, "Building sendVCSECInformationRequest");
      unsigned char message_buffer[UniversalMessage_RoutableMessage_size];
      size_t message_length = 0;
      int return_code = tesla_ble_client_->buildVCSECInformationRequestMessage(
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

    void TeslaBLEVehicle::enqueueVCSECInformationRequest(bool force)
    {
      ESP_LOGD(TAG, "Enqueueing VCSECInformationRequest");
      std::string action_str = "data update";
      if (force)
      {
        action_str = "data update | forced";
      }

      command_queue_.emplace(
          force ? UniversalMessage_Domain_DOMAIN_INFOTAINMENT : UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY, [this]()
          {
        int return_code = this->sendVCSECInformationRequest();
        if (return_code != 0)
        {
          ESP_LOGE(TAG, "Failed to send VCSECInformationRequest");
          return return_code;
        }
        return 0; },
          action_str);
    }

    // combined function for setting charging parameters
    int TeslaBLEVehicle::sendCarServerVehicleActionMessage(BLE_CarServer_VehicleAction action, int param)
    {
      std::string action_str;
      switch (action)
      {
      case SET_SENTRY_SWITCH:
        action_str = "setSentrySwitch";
        break;
      case SET_HVAC_SWITCH:
        action_str = "setHVACSwitch";
        break;
      case SET_HVAC_STEERING_HEATER_SWITCH:
        action_str = "setHVACSteeringHeatSwitch";
        break;
      case SET_CHARGING_SWITCH:
        action_str = "setChargingSwitch";
        break;
      case SET_CHARGING_AMPS:
        action_str = "setChargingAmps";
        break;
      case SET_CHARGING_LIMIT:
        action_str = "setChargingLimit";
        break;
      case SET_OPEN_CHARGE_PORT_DOOR:
        action_str = "setOpenChargePortDoor";
        break;
      case SET_CLOSE_CHARGE_PORT_DOOR:
        action_str = "setCloseChargePortDoor";
        break;
      default:
        action_str = "setChargingParameters";
        break;
      }
      ESP_LOGI(TAG, "[%s] Adding command to queue (param=%d)", action_str.c_str(), static_cast<int>(param));
      command_queue_.emplace(
          UniversalMessage_Domain_DOMAIN_INFOTAINMENT, [this, action, action_str, param]()
          {
        unsigned char message_buffer[UniversalMessage_RoutableMessage_size];
        size_t message_length = 0;
        int return_code = 0;
        ESP_LOGI(TAG, "[%s] Building message..", action_str.c_str());
        switch (action)
        {
        case SET_SENTRY_SWITCH:
          return_code = tesla_ble_client_->buildSentrySwitchMessage(
              static_cast<bool>(param), message_buffer, &message_length);
          break;
        case SET_HVAC_SWITCH:
          return_code = tesla_ble_client_->buildHVACMessage(
              static_cast<bool>(param), message_buffer, &message_length);
          break;
        case SET_HVAC_STEERING_HEATER_SWITCH:
          return_code = tesla_ble_client_->buildHVACSteeringHeaterMessage(
              static_cast<bool>(param), message_buffer, &message_length);
          break;
        case SET_CHARGING_SWITCH:
          return_code = tesla_ble_client_->buildChargingSwitchMessage(
              static_cast<bool>(param), message_buffer, &message_length);
          break;
        case SET_CHARGING_AMPS:
          return_code = tesla_ble_client_->buildChargingAmpsMessage(
              static_cast<int32_t>(param), message_buffer, &message_length);
          break;
        case SET_CHARGING_LIMIT:
          return_code = tesla_ble_client_->buildChargingSetLimitMessage(
              static_cast<int32_t>(param), message_buffer, &message_length);
          break;
        case SET_OPEN_CHARGE_PORT_DOOR:
          return_code = tesla_ble_client_->buildOpenChargePortDoorMessage(
              message_buffer, &message_length);
          break;
        case SET_CLOSE_CHARGE_PORT_DOOR:
          return_code = tesla_ble_client_->buildCloseChargePortDoorMessage(
              message_buffer, &message_length);
          break;
        default:
          ESP_LOGE(TAG, "Invalid action: %d", static_cast<int>(action));
          return 1;
        }
        if (return_code != 0)
        {
          ESP_LOGE(TAG, "[%s] Failed to build message", action_str.c_str());
          auto session = tesla_ble_client_->getPeer(UniversalMessage_Domain_DOMAIN_INFOTAINMENT);
          if (return_code == TeslaBLE::TeslaBLE_Status_E_ERROR_INVALID_SESSION)
          {
            invalidateSession(UniversalMessage_Domain_DOMAIN_INFOTAINMENT);
          }
          return return_code;
        }

        return_code = writeBLE(message_buffer, message_length, ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
        if (return_code != 0)
        {
          ESP_LOGE(TAG, "[%s] Failed to send message", action_str.c_str());
          return return_code;
        }
        return 0; },
          action_str);
      return 0;
    }

    int TeslaBLEVehicle::handleSessionInfoUpdate(UniversalMessage_RoutableMessage message, UniversalMessage_Domain domain)
    {
      ESP_LOGD(TAG, "Received session info response from domain %s", domain_to_string(domain));

      auto session = tesla_ble_client_->getPeer(domain);

      // parse session info
      UniversalMessage_RoutableMessage_session_info_t sessionInfo = message.payload.session_info;
      Signatures_SessionInfo session_info = Signatures_SessionInfo_init_default;
      int return_code = tesla_ble_client_->parsePayloadSessionInfo(&message.payload.session_info, &session_info);
      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed to parse session info response");
        return return_code;
      }
      log_session_info(TAG, &session_info);

      switch (session_info.status)
      {
      case Signatures_Session_Info_Status_SESSION_INFO_STATUS_OK:
        ESP_LOGD(TAG, "Session is valid: key paired with vehicle");
        break;
      case Signatures_Session_Info_Status_SESSION_INFO_STATUS_KEY_NOT_ON_WHITELIST:
        ESP_LOGE(TAG, "Session is invalid: Key not on whitelist");
        return 1;
      };

      ESP_LOGD(TAG, "Updating session info..");
      return_code = session->updateSession(&session_info);
      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed to update session info");
        return return_code;
      }

      // save session info to NVS
      return_code = nvs_save_session_info(session_info, domain);
      if (return_code != 0)
      {
        ESP_LOGE(TAG, "Failed to save %s session info to NVS", domain_to_string(domain));
      }

      if (!command_queue_.empty())
      {
        BLECommand &current_command = command_queue_.front();
        if (domain == UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY && current_command.state == BLECommandState::WAITING_FOR_VCSEC_AUTH_RESPONSE)
        {
          switch (current_command.domain)
          {
          case UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY:
            ESP_LOGV(TAG, "[%s] VCSEC authenticated, ready to execute",
                     current_command.execute_name.c_str());
            current_command.state = BLECommandState::READY;
            current_command.retry_count = 0;
            break;
          case UniversalMessage_Domain_DOMAIN_INFOTAINMENT:
            ESP_LOGV(TAG, "[%s] VCSEC authenticated, queuing INFOTAINMENT auth",
                     current_command.execute_name.c_str());
            current_command.state = BLECommandState::WAITING_FOR_INFOTAINMENT_AUTH;
            current_command.retry_count = 0;
            break;
          case UniversalMessage_Domain_DOMAIN_BROADCAST:
            ESP_LOGE(TAG, "[%s] Invalid state: VCSEC authenticated but no auth required", current_command.execute_name.c_str());
            // pop command
            command_queue_.pop();
            break;
          }
        }
        else if (domain == UniversalMessage_Domain_DOMAIN_INFOTAINMENT && current_command.state == BLECommandState::WAITING_FOR_INFOTAINMENT_AUTH_RESPONSE)
        {
          ESP_LOGV(TAG, "[%s] INFOTAINMENT authenticated, ready to execute",
                   current_command.execute_name.c_str());
          current_command.state = BLECommandState::READY;
          current_command.retry_count = 0;
        }
      }
      return 0;
    }

    void TeslaBLEVehicle::invalidateSession(UniversalMessage_Domain domain)
    {
      auto session = tesla_ble_client_->getPeer(domain);
      session->setIsValid(false);
      // check if we need to update the state in the command queue
      if (!command_queue_.empty())
      {
        BLECommand &current_command = command_queue_.front();
        switch (current_command.domain)
        {
        case UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY:
          if (domain == UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY)
          {
            ESP_LOGW(TAG, "[%s] VCSEC session invalid, requesting new session info..", current_command.execute_name.c_str());
            current_command.state = BLECommandState::WAITING_FOR_VCSEC_AUTH;
          }
          break;
        case UniversalMessage_Domain_DOMAIN_INFOTAINMENT:
          if (domain == UniversalMessage_Domain_DOMAIN_INFOTAINMENT)
          {
            ESP_LOGW(TAG, "[%s] INFOTAINMENT session invalid, requesting new session info..", current_command.execute_name.c_str());
            current_command.state = BLECommandState::WAITING_FOR_INFOTAINMENT_AUTH;
          }
          break;
        default:
          break;
        }
      }
    }

    int TeslaBLEVehicle::handleVCSECVehicleStatus(VCSEC_VehicleStatus vehicleStatus)
    {
      log_vehicle_status(TAG, &vehicleStatus);
      switch (vehicleStatus.vehicleSleepStatus)
      {
      case VCSEC_VehicleSleepStatus_E_VEHICLE_SLEEP_STATUS_AWAKE:
        this->updateIsAsleep(false);
        break;
      case VCSEC_VehicleSleepStatus_E_VEHICLE_SLEEP_STATUS_ASLEEP:
        this->updateIsAsleep(true);
        break;
      case VCSEC_VehicleSleepStatus_E_VEHICLE_SLEEP_STATUS_UNKNOWN:
      default:
        this->updateIsAsleep(NAN);
        break;
      } // switch vehicleSleepStatus

      switch (vehicleStatus.userPresence)
      {
      case VCSEC_UserPresence_E_VEHICLE_USER_PRESENCE_PRESENT:
        this->updateIsUserPresent(true);
        break;
      case VCSEC_UserPresence_E_VEHICLE_USER_PRESENCE_NOT_PRESENT:
        this->updateIsUserPresent(false);
        break;
      case VCSEC_UserPresence_E_VEHICLE_USER_PRESENCE_UNKNOWN:
      default:
        this->updateIsUserPresent(NAN);
        break;
      } // switch userPresence

      switch (vehicleStatus.vehicleLockState)
      {
      case VCSEC_VehicleLockState_E_VEHICLELOCKSTATE_UNLOCKED:
      case VCSEC_VehicleLockState_E_VEHICLELOCKSTATE_SELECTIVE_UNLOCKED:
        this->updateisUnlocked(true);
        break;
      case VCSEC_VehicleLockState_E_VEHICLELOCKSTATE_LOCKED:
      case VCSEC_VehicleLockState_E_VEHICLELOCKSTATE_INTERNAL_LOCKED:
        this->updateisUnlocked(false);
        break;
      default:
        this->updateisUnlocked(NAN);
        break;
      } // switch vehicleLockState

      if (vehicleStatus.vehicleSleepStatus == VCSEC_VehicleSleepStatus_E_VEHICLE_SLEEP_STATUS_AWAKE)
      {
        if (!this->isChargeFlapOpenSensor->has_state())
        {
          this->setChargeFlapHasState(true);
        }
        if (vehicleStatus.has_closureStatuses)
        {
          switch (vehicleStatus.closureStatuses.chargePort)
          {
          case VCSEC_ClosureState_E_CLOSURESTATE_OPEN:
            this->updateIsChargeFlapOpen(true);
            break;
          case VCSEC_ClosureState_E_CLOSURESTATE_CLOSED:
            this->updateIsChargeFlapOpen(false);
            break;
          default:
            break;
          } // switch chargePort
        }
        else
        {
          this->updateIsChargeFlapOpen(false);
        }
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
          this->status_clear_warning();
          this->setSensors(true);

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

      case ESP_GATTC_CLOSE_EVT:
      {
        ESP_LOGW(TAG, "BLE connection closed!");
        this->node_state = espbt::ClientState::IDLE;

        // set binary sensors to unknown
        this->setSensors(false);
        this->setChargeFlapHasState(false);

        // TODO: charging switch off
        this->status_set_warning("BLE connection closed");
        break;
      }

      case ESP_GATTC_DISCONNECT_EVT:
      {
        this->handle_ = 0;
        this->read_handle_ = 0;
        this->write_handle_ = 0;
        this->node_state = espbt::ClientState::DISCONNECTING;
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

        unsigned char private_key_buffer[PRIVATE_KEY_SIZE];
        size_t private_key_length = 0;
        int return_code = tesla_ble_client_->getPrivateKey(private_key_buffer, sizeof(private_key_buffer),
                                                           &private_key_length);
        if (return_code != 0)
        {
          ESP_LOGE(TAG, "Failed to get private key");
          break;
        }
        ESP_LOGD(TAG, "Loaded private key");

        unsigned char public_key_buffer[PUBLIC_KEY_SIZE];
        size_t public_key_length;
        return_code = tesla_ble_client_->getPublicKey(public_key_buffer, &public_key_length);
        if (return_code != 0)
        {
          ESP_LOGE(TAG, "Failed to get public key");
          break;
        }
        ESP_LOGD(TAG, "Loaded public key");
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
        // copy notify value to buffer
        std::vector<unsigned char> buffer(param->notify.value, param->notify.value + param->notify.value_len);
        ble_read_queue_.emplace(buffer);
        break;
      }

      default:
        ESP_LOGD(TAG, "Unhandled GATTC event %d", event);
        break;
      }
    }
  } // namespace tesla_ble_vehicle
} // namespace esphome
