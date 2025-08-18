#include "command_manager.h"
#include "tesla_ble_vehicle.h"
#include "common_impl.h"
#include <client.h>
#include "log.h"

namespace esphome {
namespace tesla_ble_vehicle {

BLECommand::BLECommand(UniversalMessage_Domain d, std::function<int()> e, std::string n)
    : domain(d), execute(std::move(e)), execute_name(std::move(n)), 
      state(BLECommandState::IDLE), started_at(millis()), last_tx_at(0), retry_count(0) {}

CommandManager::CommandManager(TeslaBLEVehicle* parent)
    : parent_(parent) {}

void CommandManager::enqueue_command(UniversalMessage_Domain domain, 
                                    std::function<int()> execute, 
                                    const std::string& name) {
    ESP_LOGD(COMMAND_MANAGER_TAG, "Enqueueing command: %s (domain: %s)", 
             name.c_str(), domain_to_string(domain));
    
    command_queue_.emplace(domain, std::move(execute), name);
}

void CommandManager::process_command_queue() {
    if (command_queue_.empty()) {
        return;
    }

    BLECommand& current_command = command_queue_.front();
    uint32_t now = millis();

    // Check for overall command timeout
    if (now - current_command.started_at > COMMAND_TIMEOUT) {
        ESP_LOGE(COMMAND_MANAGER_TAG, "[%s] Command timed out after %d ms",
                 current_command.execute_name.c_str(), COMMAND_TIMEOUT);
        mark_command_failed("Overall timeout");
        return;
    }

    // Process command based on current state
    switch (current_command.state) {
        case BLECommandState::IDLE:
            process_idle_command(current_command);
            break;
            
        case BLECommandState::WAITING_FOR_VCSEC_AUTH:
        case BLECommandState::WAITING_FOR_VCSEC_AUTH_RESPONSE:
        case BLECommandState::WAITING_FOR_INFOTAINMENT_AUTH:
        case BLECommandState::WAITING_FOR_INFOTAINMENT_AUTH_RESPONSE:
        case BLECommandState::WAITING_FOR_WAKE:
        case BLECommandState::WAITING_FOR_WAKE_RESPONSE:
            process_auth_waiting_command(current_command);
            break;
            
        case BLECommandState::READY:
            process_ready_command(current_command);
            break;
            
        case BLECommandState::WAITING_FOR_RESPONSE:
            // Check for response timeout
            if (now - current_command.last_tx_at > MAX_LATENCY) {
                ESP_LOGW(COMMAND_MANAGER_TAG, "[%s] Response timeout, retrying",
                         current_command.execute_name.c_str());
                current_command.state = BLECommandState::READY;
            }
            break;
    }
}

void CommandManager::process_idle_command(BLECommand& command) {
    ESP_LOGV(COMMAND_MANAGER_TAG, "[%s] Preparing command", command.execute_name.c_str());
    
    command.started_at = millis();
    
    switch (command.domain) {
        case UniversalMessage_Domain_DOMAIN_BROADCAST:
            ESP_LOGD(COMMAND_MANAGER_TAG, "[%s] No auth required, executing command", 
                     command.execute_name.c_str());
            command.state = BLECommandState::READY;
            break;
            
        case UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY:
            ESP_LOGD(COMMAND_MANAGER_TAG, "[%s] VCSEC auth required", 
                     command.execute_name.c_str());
            initiate_vcsec_auth(command);
            break;
            
        case UniversalMessage_Domain_DOMAIN_INFOTAINMENT:
            ESP_LOGD(COMMAND_MANAGER_TAG, "[%s] Infotainment auth required", 
                     command.execute_name.c_str());
            initiate_infotainment_auth(command);
            break;
            
        default:
            ESP_LOGE(COMMAND_MANAGER_TAG, "[%s] Unknown domain: %d", 
                     command.execute_name.c_str(), command.domain);
            mark_command_failed("Unknown domain");
            break;
    }
}

void CommandManager::process_auth_waiting_command(BLECommand& command) {
    uint32_t now = millis();
    
    // Check for auth timeout
    if (now - command.last_tx_at > MAX_LATENCY) {
        switch (command.state) {
            case BLECommandState::WAITING_FOR_VCSEC_AUTH:
                initiate_vcsec_auth(command);
                break;
                
            case BLECommandState::WAITING_FOR_VCSEC_AUTH_RESPONSE:
                ESP_LOGW(COMMAND_MANAGER_TAG, "[%s] VCSEC auth response timeout, retrying",
                         command.execute_name.c_str());
                command.state = BLECommandState::WAITING_FOR_VCSEC_AUTH;
                break;
                
            case BLECommandState::WAITING_FOR_INFOTAINMENT_AUTH:
                initiate_infotainment_auth(command);
                break;
                
            case BLECommandState::WAITING_FOR_INFOTAINMENT_AUTH_RESPONSE:
                ESP_LOGW(COMMAND_MANAGER_TAG, "[%s] Infotainment auth response timeout, retrying",
                         command.execute_name.c_str());
                command.state = BLECommandState::WAITING_FOR_INFOTAINMENT_AUTH;
                break;
                
            case BLECommandState::WAITING_FOR_WAKE:
                initiate_wake_sequence(command);
                break;
                
            case BLECommandState::WAITING_FOR_WAKE_RESPONSE:
                // Check if vehicle is now awake
                if (!parent_->get_state_manager()->is_asleep()) {
                    ESP_LOGI(COMMAND_MANAGER_TAG, "[%s] Vehicle is now awake", 
                             command.execute_name.c_str());
                    // Determine next state based on command requirements
                    if (command.domain == UniversalMessage_Domain_DOMAIN_INFOTAINMENT) {
                        command.state = BLECommandState::WAITING_FOR_INFOTAINMENT_AUTH;
                    } else if (command.domain == UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY) {
                        // VCSEC commands that went through wake should be ready now
                        command.state = BLECommandState::READY;
                    } else {
                        // Broadcast commands should be ready
                        command.state = BLECommandState::READY;
                    }
                } else {
                    retry_command(command);
                }
                break;
                
            default:
                break;
        }
    }
}

void CommandManager::process_ready_command(BLECommand& command) {
    uint32_t now = millis();
    
    if (now - command.last_tx_at > MAX_LATENCY) {
        if (command.retry_count >= MAX_RETRIES) {
            ESP_LOGE(COMMAND_MANAGER_TAG, "[%s] Max retries exceeded", 
                     command.execute_name.c_str());
            mark_command_failed("Max retries exceeded");
            return;
        }
        
        ESP_LOGI(COMMAND_MANAGER_TAG, "[%s] Executing command (attempt %d/%d)", 
                 command.execute_name.c_str(), command.retry_count + 1, MAX_RETRIES + 1);
        
        int result = command.execute();
        command.last_tx_at = now;
        command.retry_count++;
        
        if (result == 0) {
            command.state = BLECommandState::WAITING_FOR_RESPONSE;
        } else {
            ESP_LOGE(COMMAND_MANAGER_TAG, "[%s] Command execution failed (error: %d)", 
                     command.execute_name.c_str(), result);
            retry_command(command);
        }
    }
}

void CommandManager::initiate_vcsec_auth(BLECommand& command) {
    if (is_domain_authenticated(UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY)) {
        // Already authenticated, proceed based on target domain
        if (command.domain == UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY) {
            command.state = BLECommandState::READY;
        } else {
            // Need infotainment auth, proceed to next step
            command.state = BLECommandState::WAITING_FOR_INFOTAINMENT_AUTH;
        }
    } else {
        // Request VCSEC session info
        auto* session_manager = parent_->get_session_manager();
        if (session_manager && session_manager->request_session_info(UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY)) {
            command.state = BLECommandState::WAITING_FOR_VCSEC_AUTH_RESPONSE;
            command.last_tx_at = millis();
        } else {
            ESP_LOGE(COMMAND_MANAGER_TAG, "[%s] Failed to request VCSEC session info", 
                     command.execute_name.c_str());
            mark_command_failed("VCSEC auth request failed");
        }
    }
}

void CommandManager::initiate_infotainment_auth(BLECommand& command) {
    // Check if vehicle is asleep - if so, transition to wake state
    if (parent_->get_state_manager()->is_asleep()) {
        ESP_LOGD(COMMAND_MANAGER_TAG, "[%s] Vehicle is asleep, transitioning to wake state", 
                 command.execute_name.c_str());
        command.state = BLECommandState::WAITING_FOR_WAKE;
        command.last_tx_at = 0;  // Trigger immediate wake sequence
        return;
    }
    
    // Check if VCSEC auth is required first
    if (!is_domain_authenticated(UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY)) {
        ESP_LOGD(COMMAND_MANAGER_TAG, "[%s] VCSEC auth required before infotainment auth", 
                 command.execute_name.c_str());
        command.state = BLECommandState::WAITING_FOR_VCSEC_AUTH;
        return;
    }
    
    if (is_domain_authenticated(UniversalMessage_Domain_DOMAIN_INFOTAINMENT)) {
        command.state = BLECommandState::READY;
    } else {
        // Request infotainment session info
        auto* session_manager = parent_->get_session_manager();
        if (session_manager && session_manager->request_session_info(UniversalMessage_Domain_DOMAIN_INFOTAINMENT)) {
            command.state = BLECommandState::WAITING_FOR_INFOTAINMENT_AUTH_RESPONSE;
            command.last_tx_at = millis();
        } else {
            ESP_LOGE(COMMAND_MANAGER_TAG, "[%s] Failed to request infotainment session info", 
                     command.execute_name.c_str());
            mark_command_failed("Infotainment auth request failed");
        }
    }
}

void CommandManager::initiate_wake_sequence(BLECommand& command) {
    ESP_LOGD(COMMAND_MANAGER_TAG, "[%s] Sending wake command", command.execute_name.c_str());
    
    // Execute wake command directly to avoid recursive queue operations
    auto wake_command = BLECommandHelper::create_command(parent_, [](auto* client, auto* buffer, auto* length) {
        return client->buildVCSECActionMessage(
            VCSEC_RKEAction_E_RKE_ACTION_WAKE_VEHICLE,
            buffer, length);
    });
    
    int result = wake_command();
    if (result == 0) {
        command.state = BLECommandState::WAITING_FOR_WAKE_RESPONSE;
        command.last_tx_at = millis();
    } else {
        ESP_LOGE(COMMAND_MANAGER_TAG, "[%s] Failed to send wake command: %d", 
                 command.execute_name.c_str(), result);
        mark_command_failed("Wake command failed");
    }
}

void CommandManager::retry_command(BLECommand& command) {
    if (command.retry_count >= MAX_RETRIES) {
        mark_command_failed("Max retries exceeded");
    } else {
        ESP_LOGD(COMMAND_MANAGER_TAG, "[%s] Retrying command (attempt %d/%d)", 
                 command.execute_name.c_str(), command.retry_count + 1, MAX_RETRIES + 1);
        
        // Reset to appropriate state based on current state
        switch (command.state) {
            case BLECommandState::WAITING_FOR_WAKE_RESPONSE:
                // Retry wake sequence
                command.state = BLECommandState::WAITING_FOR_WAKE;
                break;
                
            case BLECommandState::WAITING_FOR_RESPONSE:
                // Retry command execution
                command.state = BLECommandState::READY;
                break;
                
            default:
                // For auth states and others, restart from IDLE
                command.state = BLECommandState::IDLE;
                break;
        }
        
        command.retry_count++;
        command.last_tx_at = 0;  // Trigger immediate processing
    }
}

bool CommandManager::is_domain_authenticated(UniversalMessage_Domain domain) {
    auto* session_manager = parent_->get_session_manager();
    return session_manager ? session_manager->is_domain_authenticated(domain) : false;
}

void CommandManager::handle_authentication_response(UniversalMessage_Domain domain, bool success) {
    if (command_queue_.empty()) {
        return;
    }
    
    BLECommand& current_command = command_queue_.front();
    
    if (success) {
        ESP_LOGD(COMMAND_MANAGER_TAG, "[%s] Authentication successful for %s", 
                 current_command.execute_name.c_str(), domain_to_string(domain));
        
        // Move to next state based on command requirements
        if (domain == UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY) {
            if (current_command.domain == UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY) {
                current_command.state = BLECommandState::READY;
            } else if (current_command.domain == UniversalMessage_Domain_DOMAIN_INFOTAINMENT) {
                // Continue with infotainment auth
                current_command.state = BLECommandState::WAITING_FOR_INFOTAINMENT_AUTH;
            } else {
                current_command.state = BLECommandState::READY;
            }
        } else if (domain == UniversalMessage_Domain_DOMAIN_INFOTAINMENT) {
            current_command.state = BLECommandState::READY;
        }
        
        // Reset timing for next state
        current_command.last_tx_at = 0;
    } else {
        ESP_LOGE(COMMAND_MANAGER_TAG, "[%s] Authentication failed for %s", 
                 current_command.execute_name.c_str(), domain_to_string(domain));
        mark_command_failed("Authentication failed");
    }
}

BLECommand* CommandManager::get_current_command() {
    if (command_queue_.empty()) {
        return nullptr;
    }
    return &command_queue_.front();
}

void CommandManager::mark_command_completed() {
    if (!command_queue_.empty()) {
        BLECommand& command = command_queue_.front();
        uint32_t duration = millis() - command.started_at;
        ESP_LOGI(COMMAND_MANAGER_TAG, "[%s] Command completed successfully in %u ms", 
                 command.execute_name.c_str(), duration);
        command_queue_.pop();
    }
}

void CommandManager::mark_command_failed(const std::string& reason) {
    if (!command_queue_.empty()) {
        BLECommand& command = command_queue_.front();
        uint32_t duration = millis() - command.started_at;
        ESP_LOGE(COMMAND_MANAGER_TAG, "[%s] Command failed after %u ms: %s", 
                 command.execute_name.c_str(), duration, reason.c_str());
        command_queue_.pop();
    }
}

void CommandManager::clear_queue() {
    std::queue<BLECommand> empty_queue;
    command_queue_.swap(empty_queue);
    ESP_LOGD(COMMAND_MANAGER_TAG, "Command queue cleared");
}

void CommandManager::update_command_state(BLECommandState new_state) {
    if (!command_queue_.empty()) {
        command_queue_.front().state = new_state;
        ESP_LOGV(COMMAND_MANAGER_TAG, "Command state updated to %d", static_cast<int>(new_state));
    } else {
        ESP_LOGW(COMMAND_MANAGER_TAG, "Attempted to update command state but queue is empty");
    }
}

} // namespace tesla_ble_vehicle
} // namespace esphome
