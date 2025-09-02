#include "message_handler.h"
#include "tesla_ble_vehicle.h"
#include <client.h>
#include "log.h"
#include <esphome/core/helpers.h>

namespace esphome {
namespace tesla_ble_vehicle {

MessageHandler::MessageHandler(TeslaBLEVehicle* parent)
    : parent_(parent) {}

void MessageHandler::add_response(const UniversalMessage_RoutableMessage& message) {
    response_queue_.push(message);
    ESP_LOGV(MESSAGE_HANDLER_TAG, "Added message to response queue (queue size: %zu)", response_queue_.size());
}

void MessageHandler::process_response_queue() {
    if (response_queue_.empty()) {
        return;
    }

    ESP_LOGV(MESSAGE_HANDLER_TAG, "Processing response queue (size: %zu)", response_queue_.size());
    
    UniversalMessage_RoutableMessage message = response_queue_.front();
    response_queue_.pop();

    handle_universal_message(message);
}

void MessageHandler::handle_universal_message(const UniversalMessage_RoutableMessage& message) {
    if (!validate_message(message)) {
        // ESP_LOGV(MESSAGE_HANDLER_TAG, "Dropping invalid message");
        return;
    }

    log_message_details(message);

    // Handle session info updates first
    if (message.which_payload == UniversalMessage_RoutableMessage_session_info_tag) {
        UniversalMessage_Domain domain = message.from_destination.sub_destination.domain;
        handle_session_info(message, domain);
        return;
    }

    // Handle signed message status
    if (message.has_signedMessageStatus) {
        ESP_LOGD(MESSAGE_HANDLER_TAG, "Received signed message status");
        log_message_status(MESSAGE_HANDLER_TAG, &message.signedMessageStatus);
        
        if (message.signedMessageStatus.operation_status == UniversalMessage_OperationStatus_E_OPERATIONSTATUS_ERROR) {
            // Reset authentication for domain
            UniversalMessage_Domain domain = message.from_destination.sub_destination.domain;
            auto* session_manager = parent_->get_session_manager();
            if (session_manager) {
                session_manager->invalidate_session(domain);
            }
            
            // Update command state
            update_command_state_on_response(message);
            return;
        }
    }

    // Route message based on source domain
    switch (message.from_destination.which_sub_destination) {
        case UniversalMessage_Destination_domain_tag:
            switch (message.from_destination.sub_destination.domain) {
                case UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY:
                    handle_vcsec_message(message);
                    break;
                case UniversalMessage_Domain_DOMAIN_INFOTAINMENT:
                    handle_carserver_message(message);
                    break;
                default:
                    ESP_LOGD(MESSAGE_HANDLER_TAG, "Message from unknown domain: %s", 
                             domain_to_string(message.from_destination.sub_destination.domain));
                    break;
            }
            break;
            
        case UniversalMessage_Destination_routing_address_tag:
            ESP_LOGD(MESSAGE_HANDLER_TAG, "Received message from routing address");
            break;
            
        default:
            ESP_LOGD(MESSAGE_HANDLER_TAG, "Message from unknown destination type: %d", 
                     message.from_destination.which_sub_destination);
            break;
    }
}

void MessageHandler::handle_vcsec_message(const UniversalMessage_RoutableMessage& message) {
    ESP_LOGD(MESSAGE_HANDLER_TAG, "Processing VCSEC message");
    
    auto* session_manager = parent_->get_session_manager();
    if (!session_manager) {
        ESP_LOGE(MESSAGE_HANDLER_TAG, "Session manager not available");
        return;
    }
    
    auto* client = session_manager->get_client();
    if (!client) {
        ESP_LOGE(MESSAGE_HANDLER_TAG, "Tesla client not available");
        return;
    }
    
    VCSEC_FromVCSECMessage vcsec_message = VCSEC_FromVCSECMessage_init_default;
    int result = client->parseFromVCSECMessage(
        const_cast<UniversalMessage_RoutableMessage_protobuf_message_as_bytes_t*>(&message.payload.protobuf_message_as_bytes), &vcsec_message);
    
    if (result != 0) {
        ESP_LOGE(MESSAGE_HANDLER_TAG, "Failed to parse VCSEC message: %d", result);
        return;
    }
    
    ESP_LOGD(MESSAGE_HANDLER_TAG, "Parsed VCSEC message successfully");

    switch (vcsec_message.which_sub_message) {
        case VCSEC_FromVCSECMessage_vehicleStatus_tag:
            ESP_LOGD(MESSAGE_HANDLER_TAG, "Received vehicle status");
            handle_vehicle_status(vcsec_message.sub_message.vehicleStatus);
            break;
            
        case VCSEC_FromVCSECMessage_commandStatus_tag:
            ESP_LOGD(MESSAGE_HANDLER_TAG, "Received VCSEC command status");
            log_vcsec_command_status(MESSAGE_HANDLER_TAG, &vcsec_message.sub_message.commandStatus);
            update_command_state_on_response(message);
            break;
            
        case VCSEC_FromVCSECMessage_whitelistInfo_tag:
            ESP_LOGD(MESSAGE_HANDLER_TAG, "Received whitelist info");
            // Could update pairing status here
            break;
            
        case VCSEC_FromVCSECMessage_whitelistEntryInfo_tag:
            ESP_LOGD(MESSAGE_HANDLER_TAG, "Received whitelist entry info");
            break;
            
        case VCSEC_FromVCSECMessage_nominalError_tag:
            ESP_LOGE(MESSAGE_HANDLER_TAG, "Received nominal error: %s", 
                     generic_error_to_string(vcsec_message.sub_message.nominalError.genericError));
            break;
            
        default:
            // Probably information request with public key
            VCSEC_InformationRequest info_message = VCSEC_InformationRequest_init_default;
            result = client->parseVCSECInformationRequest(
                const_cast<UniversalMessage_RoutableMessage_protobuf_message_as_bytes_t*>(&message.payload.protobuf_message_as_bytes), &info_message);
            
            if (result == 0) {
                ESP_LOGD(MESSAGE_HANDLER_TAG, "Parsed VCSEC InformationRequest message");
                ESP_LOGD(MESSAGE_HANDLER_TAG, "InformationRequest public key: %s", 
                         format_hex(info_message.key.publicKey.bytes, info_message.key.publicKey.size).c_str());
            } else {
                ESP_LOGW(MESSAGE_HANDLER_TAG, "Unknown VCSEC message type");
            }
            break;
    }
}

void MessageHandler::handle_carserver_message(const UniversalMessage_RoutableMessage& message) {
    ESP_LOGD(MESSAGE_HANDLER_TAG, "Processing CarServer message");
    
    auto* session_manager = parent_->get_session_manager();
    if (!session_manager) {
        ESP_LOGE(MESSAGE_HANDLER_TAG, "Session manager not available");
        return;
    }
    
    auto* client = session_manager->get_client();
    if (!client) {
        ESP_LOGE(MESSAGE_HANDLER_TAG, "Tesla client not available");
        return;
    }
    
    CarServer_Response carserver_response = CarServer_Response_init_default;
    
    // Extract signature data and fault information from the message
    const Signatures_SignatureData* sig_data = nullptr;
    UniversalMessage_MessageFault_E fault = UniversalMessage_MessageFault_E_MESSAGEFAULT_ERROR_NONE;
    
    if (message.which_sub_sigData == UniversalMessage_RoutableMessage_signature_data_tag) {
        sig_data = &message.sub_sigData.signature_data;
    }
    
    if (message.has_signedMessageStatus) {
        fault = message.signedMessageStatus.signed_message_fault;
    }
    
    // Log if there's a message fault
    if (fault != UniversalMessage_MessageFault_E_MESSAGEFAULT_ERROR_NONE) {
        ESP_LOGW(MESSAGE_HANDLER_TAG, "Message fault detected: %s", message_fault_to_string(fault));
    }
    
    ESP_LOGD(MESSAGE_HANDLER_TAG, "Starting parsePayloadCarServerResponse...");
    ESP_LOGD(MESSAGE_HANDLER_TAG, "Payload size: %d bytes", message.payload.protobuf_message_as_bytes.size);
    
    int result = session_manager->get_client()->parsePayloadCarServerResponse(
        const_cast<UniversalMessage_RoutableMessage_protobuf_message_as_bytes_t*>(&message.payload.protobuf_message_as_bytes), const_cast<Signatures_SignatureData*>(sig_data), message.which_sub_sigData, fault, &carserver_response);
    
    ESP_LOGD(MESSAGE_HANDLER_TAG, "parsePayloadCarServerResponse completed with return_code: %d", result);
    
    if (result != 0) {
        ESP_LOGE(MESSAGE_HANDLER_TAG, "Failed to parse CarServer response: %d", result);
        update_command_state_on_response(message);
        return;
    }
    
    ESP_LOGD(MESSAGE_HANDLER_TAG, "Parsed CarServer.Response successfully");
    log_carserver_response(MESSAGE_HANDLER_TAG, &carserver_response);
    
    // Handle the response
    handle_carserver_response(carserver_response);
    
    // Check for action status and update command state
    if (carserver_response.has_actionStatus) {
        auto* command_manager = parent_->get_command_manager();
        if (command_manager && command_manager->has_pending_commands()) {
            auto* current_command = command_manager->get_current_command();
            if (current_command && current_command->domain == UniversalMessage_Domain_DOMAIN_INFOTAINMENT) {
                switch (carserver_response.actionStatus.result) {
                    case CarServer_OperationStatus_E_OPERATIONSTATUS_OK:
                        {
                            uint32_t duration = millis() - current_command->started_at;
                            ESP_LOGV(MESSAGE_HANDLER_TAG, "[%s] Command handled successfully in %u ms", 
                                     current_command->execute_name.c_str(), duration);
                        }
                        command_manager->mark_command_completed();
                        break;
                        
                    case CarServer_OperationStatus_E_OPERATIONSTATUS_ERROR:
                        ESP_LOGE(MESSAGE_HANDLER_TAG, "[%s] Command failed with error", 
                                 current_command->execute_name.c_str());
                        command_manager->mark_command_failed("CarServer error");
                        break;
                        
                    default:
                        ESP_LOGD(MESSAGE_HANDLER_TAG, "[%s] Command status: %d", 
                                 current_command->execute_name.c_str(), carserver_response.actionStatus.result);
                        break;
                }
            }
        }
    } else {
        // No action status - could be a data request response or missing status
        // Only mark as completed if we have a pending infotainment command
        auto* command_manager = parent_->get_command_manager();
        if (command_manager && command_manager->has_pending_commands()) {
            auto* current_command = command_manager->get_current_command();
            if (current_command && current_command->domain == UniversalMessage_Domain_DOMAIN_INFOTAINMENT) {
                ESP_LOGD(MESSAGE_HANDLER_TAG, "[%s] No action status received, assuming data request success", 
                         current_command->execute_name.c_str());
                command_manager->mark_command_completed();
            } else {
                // Not our command, just update state normally
                update_command_state_on_response(message);
            }
        } else {
            // No pending commands, just update state normally
            update_command_state_on_response(message);
        }
    }
}

void MessageHandler::handle_session_info(const UniversalMessage_RoutableMessage& message, UniversalMessage_Domain domain) {
    ESP_LOGD(MESSAGE_HANDLER_TAG, "Handling session info for %s", domain_to_string(domain));
    
    auto* session_manager = parent_->get_session_manager();
    if (!session_manager) {
        ESP_LOGE(MESSAGE_HANDLER_TAG, "Session manager not available");
        return;
    }
    
    // Parse session info
    Signatures_SessionInfo session_info = Signatures_SessionInfo_init_default;
    int result = session_manager->get_client()->parsePayloadSessionInfo(const_cast<UniversalMessage_RoutableMessage_session_info_t*>(&message.payload.session_info), &session_info);
    
    if (result != 0) {
        ESP_LOGE(MESSAGE_HANDLER_TAG, "Failed to parse session info for %s: %d", domain_to_string(domain), result);
        
        // Notify command manager of auth failure
        auto* command_manager = parent_->get_command_manager();
        if (command_manager) {
            command_manager->handle_authentication_response(domain, false);
        }
        return;
    }
    
    ESP_LOGD(MESSAGE_HANDLER_TAG, "Parsed session info successfully");
    
    // Check session status
    switch (session_info.status) {
        case Signatures_Session_Info_Status_SESSION_INFO_STATUS_OK:
            ESP_LOGD(MESSAGE_HANDLER_TAG, "Session is valid: key paired with vehicle");
            break;
        case Signatures_Session_Info_Status_SESSION_INFO_STATUS_KEY_NOT_ON_WHITELIST:
            ESP_LOGE(MESSAGE_HANDLER_TAG, "Session is invalid: Key not on whitelist");
            
            // Notify command manager of auth failure
            auto* command_manager = parent_->get_command_manager();
            if (command_manager) {
                command_manager->handle_authentication_response(domain, false);
            }
            return;
    }
    
    // Update session
    if (session_manager->update_session(session_info, domain)) {
        ESP_LOGI(MESSAGE_HANDLER_TAG, "Updated session info for %s", domain_to_string(domain));
        
        // Notify command manager of successful auth
        auto* command_manager = parent_->get_command_manager();
        if (command_manager) {
            command_manager->handle_authentication_response(domain, true);
        }
    } else {
        ESP_LOGE(MESSAGE_HANDLER_TAG, "Failed to update session info for %s", domain_to_string(domain));
        
        // Notify command manager of auth failure
        auto* command_manager = parent_->get_command_manager();
        if (command_manager) {
            command_manager->handle_authentication_response(domain, false);
        }
    }
}

void MessageHandler::handle_vehicle_status(const VCSEC_VehicleStatus& status) {
    log_vehicle_status(MESSAGE_HANDLER_TAG, &status);
    
    auto* state_manager = parent_->get_state_manager();
    if (state_manager) {
        state_manager->update_vehicle_status(status);
    }
    
    update_command_state_on_response_with_status(status);
}

void MessageHandler::handle_carserver_response(const CarServer_Response& response) {
    ESP_LOGD(MESSAGE_HANDLER_TAG, "Handling CarServer response (type: %d)", response.which_response_msg);
    
    auto* state_manager = parent_->get_state_manager();
    if (!state_manager) {
        ESP_LOGW(MESSAGE_HANDLER_TAG, "State manager not available");
        return;
    }
    
    // Check if we have vehicle data response
    if (response.which_response_msg == CarServer_Response_vehicleData_tag) {
        ESP_LOGD(MESSAGE_HANDLER_TAG, "Processing vehicle data response");
        process_vehicle_data(response.response_msg.vehicleData);
    } else {
        ESP_LOGD(MESSAGE_HANDLER_TAG, "Non-vehicle-data response received");
    }
}

void MessageHandler::process_vehicle_data(const CarServer_VehicleData& vehicle_data) {
    ESP_LOGD(MESSAGE_HANDLER_TAG, "Processing vehicle data...");
    
    auto* state_manager = parent_->get_state_manager();
    if (!state_manager) {
        return;
    }
    
    // Process charge state data
    if (vehicle_data.has_charge_state) {
        ESP_LOGD(MESSAGE_HANDLER_TAG, "Processing charge state data");
        state_manager->update_charge_state(vehicle_data.charge_state);
    }
    
    // Process climate state data if present
    if (vehicle_data.has_climate_state) {
        ESP_LOGD(MESSAGE_HANDLER_TAG, "Processing climate state data");
        state_manager->update_climate_state(vehicle_data.climate_state);
    }
    
    // Process drive state data if present
    if (vehicle_data.has_drive_state) {
        ESP_LOGD(MESSAGE_HANDLER_TAG, "Processing drive state data");
        state_manager->update_drive_state(vehicle_data.drive_state);
    }
    
    // Process location state data if present
    if (vehicle_data.has_location_state) {
        ESP_LOGD(MESSAGE_HANDLER_TAG, "Processing location state data");
        // Future implementation
    }
    
    // Process closures state data if present
    if (vehicle_data.has_closures_state) {
        ESP_LOGD(MESSAGE_HANDLER_TAG, "Processing closures state data");
        // Future implementation
    }
    
    ESP_LOGD(MESSAGE_HANDLER_TAG, "Vehicle data processing completed");
}

bool MessageHandler::validate_message(const UniversalMessage_RoutableMessage& message) {
    if (!message.has_from_destination) {
        ESP_LOGD(MESSAGE_HANDLER_TAG, "Dropping message with missing source");
        return false;
    }
    
    if (message.request_uuid.size != 0 && message.request_uuid.size != 16) {
        ESP_LOGW(MESSAGE_HANDLER_TAG, "Dropping message with invalid request UUID length");
        return false;
    }
    
    if (!message.has_to_destination) {
        ESP_LOGV(MESSAGE_HANDLER_TAG, "Dropping message with missing destination");
        return false;
    }
    
    switch (message.to_destination.which_sub_destination) {
        case UniversalMessage_Destination_domain_tag:
            // This is normal for domain messages
            break;
        case UniversalMessage_Destination_routing_address_tag:
            if (message.to_destination.sub_destination.routing_address.size != 16) {
                ESP_LOGW(MESSAGE_HANDLER_TAG, "Dropping message with invalid address length");
                return false;
            }
            break;
        default:
            ESP_LOGW(MESSAGE_HANDLER_TAG, "Dropping message with unrecognized destination type: %d", 
                     message.to_destination.which_sub_destination);
            return false;
    }
    
    return true;
}

void MessageHandler::log_message_details(const UniversalMessage_RoutableMessage& message) {
    std::string request_uuid_hex = format_hex(message.request_uuid.bytes, message.request_uuid.size);
    ESP_LOGV(MESSAGE_HANDLER_TAG, "Processing message [%s]", request_uuid_hex.c_str());
    
    if (message.has_from_destination && message.from_destination.which_sub_destination == UniversalMessage_Destination_domain_tag) {
        ESP_LOGV(MESSAGE_HANDLER_TAG, "  From domain: %s", 
                 domain_to_string(message.from_destination.sub_destination.domain));
    }
}

void MessageHandler::update_command_state_on_response(const UniversalMessage_RoutableMessage& message) {
    auto* command_manager = parent_->get_command_manager();
    if (!command_manager || !command_manager->has_pending_commands()) {
        return;
    }
    
    auto* current_command = command_manager->get_current_command();
    if (!current_command) {
        return;
    }
    
    // Only mark command as completed if it's waiting for a response
    if (current_command->state == BLECommandState::WAITING_FOR_RESPONSE) {
        ESP_LOGD(MESSAGE_HANDLER_TAG, "[%s] Command response received", 
                 current_command->execute_name.c_str());
        command_manager->mark_command_completed();
    } else {
        ESP_LOGV(MESSAGE_HANDLER_TAG, "[%s] Received response but command is in state %d", 
                 current_command->execute_name.c_str(), static_cast<int>(current_command->state));
    }
}

void MessageHandler::update_command_state_on_response_with_status(const VCSEC_VehicleStatus& status) {
    auto* command_manager = parent_->get_command_manager();
    if (!command_manager || !command_manager->has_pending_commands()) {
        return;
    }
    
    auto* current_command = command_manager->get_current_command();
    if (!current_command) {
        return;
    }
    
    // Handle wake-related commands
    if (current_command->state == BLECommandState::WAITING_FOR_WAKE_RESPONSE || 
        current_command->execute_name.find("wake") != std::string::npos) {
        
        // Check if vehicle is awake using multiple indicators
        bool is_awake = false;
        
        // Method 1: Check vehicleSleepStatus directly
        if (status.vehicleSleepStatus == VCSEC_VehicleSleepStatus_E_VEHICLE_SLEEP_STATUS_AWAKE) {
            is_awake = true;
        }
        
        // Method 2: Check state manager
        auto* state_manager = parent_->get_state_manager();
        if (state_manager && !state_manager->is_asleep()) {
            is_awake = true;
        }
        
        // Method 3: If we're getting detailed closure status, vehicle is awake
        if (status.has_closureStatuses) {
            ESP_LOGD(MESSAGE_HANDLER_TAG, "[%s] Received detailed vehicle status, assuming awake", 
                     current_command->execute_name.c_str());
            is_awake = true;
        }
        
        if (is_awake) {
            uint32_t duration = millis() - current_command->started_at;
            ESP_LOGI(MESSAGE_HANDLER_TAG, "[%s] Vehicle is now awake (command completed in %u ms)", 
                     current_command->execute_name.c_str(), duration);
            command_manager->mark_command_completed();
            return;
        }
    }
    
    // For other VCSEC commands in WAITING_FOR_RESPONSE state
    if (current_command->state == BLECommandState::WAITING_FOR_RESPONSE && 
        current_command->domain == UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY) {
        uint32_t duration = millis() - current_command->started_at;
        ESP_LOGV(MESSAGE_HANDLER_TAG, "[%s] VCSEC command handled successfully in %u ms", 
                 current_command->execute_name.c_str(), duration);
        command_manager->mark_command_completed();
    }
}

} // namespace tesla_ble_vehicle
} // namespace esphome
