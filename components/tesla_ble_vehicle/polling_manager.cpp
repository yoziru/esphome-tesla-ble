#include "polling_manager.h"
#include "tesla_ble_vehicle.h"
#include <client.h>
#include "log.h"

namespace esphome {
namespace tesla_ble_vehicle {

PollingManager::PollingManager(TeslaBLEVehicle* parent)
    : parent_(parent), last_vcsec_poll_(0), last_infotainment_poll_(0),
      connection_time_(0), just_connected_(false), is_awake_(false),
      is_charging_(false), is_unlocked_(false) {}

void PollingManager::update() {
    if (!parent_->is_connected()) {
        ESP_LOGV(POLLING_MANAGER_TAG, "Not connected, skipping polling");
        return;
    }

    ESP_LOGD(POLLING_MANAGER_TAG, "Polling manager update called (just_connected=%s)", just_connected_ ? "true" : "false");
    
    uint32_t now = millis();
    
    // Handle initial connection - always poll immediately
    if (just_connected_) {
        ESP_LOGI(POLLING_MANAGER_TAG, "Just connected - performing initial VCSEC poll");
        request_vcsec_poll();
        last_vcsec_poll_ = now;
        just_connected_ = false;
        
        // Always request infotainment poll on initial connection to populate sensors
        ESP_LOGI(POLLING_MANAGER_TAG, "Initial connection - forcing infotainment poll to populate sensors");
        request_infotainment_poll();
        last_infotainment_poll_ = now;
        
        return;
    }

    // Determine what to poll based on smart polling logic
    bool should_poll_vcsec_now = should_poll_vcsec();
    bool should_poll_infotainment_now = should_poll_infotainment();

    ESP_LOGV(POLLING_MANAGER_TAG, "Polling check: VCSEC=%s, Infotainment=%s", 
             should_poll_vcsec_now ? "yes" : "no", 
             should_poll_infotainment_now ? "yes" : "no");

    if (should_poll_vcsec_now) {
        log_polling_decision("VCSEC status poll", "Regular interval");
        request_vcsec_poll();
        last_vcsec_poll_ = now;
    }

    if (should_poll_infotainment_now) {
        std::string reason = get_fast_poll_reason();
        log_polling_decision("Infotainment data poll", reason);
        request_infotainment_poll();
        last_infotainment_poll_ = now;
    }
}

void PollingManager::handle_connection_established() {
    ESP_LOGI(POLLING_MANAGER_TAG, "BLE connection established - setting just_connected flag");
    
    connection_time_ = millis();
    just_connected_ = true;
    
    // Reset state cache
    is_awake_ = false;
    is_charging_ = false;
    is_unlocked_ = false;
    
    // Reset polling timestamps
    last_vcsec_poll_ = 0;
    last_infotainment_poll_ = 0;
    
    ESP_LOGD(POLLING_MANAGER_TAG, "Connection setup complete - ready for immediate poll");
}

void PollingManager::handle_connection_lost() {
    ESP_LOGI(POLLING_MANAGER_TAG, "BLE connection lost");
    
    just_connected_ = false;
    connection_time_ = 0;
    
    // Reset state cache
    is_awake_ = false;
    is_charging_ = false;
    is_unlocked_ = false;
    
    // Reset polling timestamps
    last_vcsec_poll_ = 0;
    last_infotainment_poll_ = 0;
}

void PollingManager::update_vehicle_state(bool is_awake, bool is_charging, bool is_unlocked) {
    bool state_changed = (is_awake_ != is_awake) || 
                        (is_charging_ != is_charging) || 
                        (is_unlocked_ != is_unlocked);
    
    if (state_changed) {
        ESP_LOGD(POLLING_MANAGER_TAG, "Vehicle state changed: awake=%s, charging=%s, unlocked=%s",
                 is_awake ? "true" : "false",
                 is_charging ? "true" : "false", 
                 is_unlocked ? "true" : "false");
        
        // If vehicle just woke up, poll infotainment immediately (bypass time interval check)
        if (!is_awake_ && is_awake) {
            ESP_LOGI(POLLING_MANAGER_TAG, "Vehicle just woke up - requesting immediate infotainment poll");
            request_infotainment_poll(true);  // true = bypass delay
            last_infotainment_poll_ = millis();
        }
    }
    
    is_awake_ = is_awake;
    is_charging_ = is_charging;
    is_unlocked_ = is_unlocked;
}

void PollingManager::force_immediate_poll() {
    ESP_LOGI(POLLING_MANAGER_TAG, "Force immediate poll requested");
    
    uint32_t now = millis();
    
    // Force VCSEC poll
    request_vcsec_poll();
    last_vcsec_poll_ = now;
    
    // Force infotainment poll if awake
    if (is_awake_) {
        request_infotainment_poll();
        last_infotainment_poll_ = now;
    }
}

bool PollingManager::should_poll_vcsec() {
    uint32_t now = millis();
    uint32_t time_since_last = now - last_vcsec_poll_;
    
    ESP_LOGV(POLLING_MANAGER_TAG, "VCSEC poll check: time_since_last=%u ms, interval=%u ms", 
             time_since_last, VCSEC_POLL_INTERVAL);
    
    // Always poll VCSEC at the regular interval - it's safe and doesn't wake the car
    if (time_since_last >= VCSEC_POLL_INTERVAL) {
        ESP_LOGD(POLLING_MANAGER_TAG, "VCSEC poll needed (%u ms since last)", time_since_last);
        return true;
    }
    
    return false;
}

bool PollingManager::should_poll_infotainment() {
    uint32_t now = millis();
    
    // Don't poll infotainment if vehicle is asleep
    if (!is_awake_) {
        ESP_LOGV(POLLING_MANAGER_TAG, "Vehicle asleep, skipping infotainment poll");
        return false;
    }
    
    uint32_t poll_interval = get_infotainment_poll_interval();
    
    if (now - last_infotainment_poll_ >= poll_interval) {
        return true;
    }
    
    return false;
}

uint32_t PollingManager::get_infotainment_poll_interval() {
    if (should_use_fast_polling()) {
        return INFOTAINMENT_POLL_CHARGING;
    } else {
        return INFOTAINMENT_POLL_AWAKE;
    }
}

bool PollingManager::should_use_fast_polling() {
    // Fast poll if charging or unlocked (user might be actively using the vehicle)
    return is_charging_ || is_unlocked_;
}

std::string PollingManager::get_fast_poll_reason() {
    if (is_charging_ && is_unlocked_) {
        return "charging and unlocked";
    } else if (is_charging_) {
        return "charging";
    } else if (is_unlocked_) {
        return "unlocked";
    } else {
        return "vehicle awake";
    }
}

void PollingManager::request_vcsec_poll() {
    ESP_LOGD(POLLING_MANAGER_TAG, "Requesting VCSEC poll");
    
    auto* command_manager = parent_->get_command_manager();
    if (!command_manager) {
        ESP_LOGE(POLLING_MANAGER_TAG, "Command manager not available");
        return;
    }
    
    command_manager->enqueue_command(
        UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY,
        [this]() {
            auto* session_manager = parent_->get_session_manager();
            auto* ble_manager = parent_->get_ble_manager();
            
            if (!session_manager || !ble_manager) {
                ESP_LOGE(POLLING_MANAGER_TAG, "Required managers not available");
                return -1;
            }
            
            auto* client = session_manager->get_client();
            if (!client) {
                ESP_LOGE(POLLING_MANAGER_TAG, "Tesla client not available");
                return -1;
            }
            
            unsigned char message_buffer[1024];
            size_t message_length = 0;
            
            int result = client->buildVCSECInformationRequestMessage(
                VCSEC_InformationRequestType_INFORMATION_REQUEST_TYPE_GET_STATUS,
                message_buffer,
                &message_length);
            
            if (result != 0) {
                ESP_LOGE(POLLING_MANAGER_TAG, "Failed to build VCSEC information request: %d", result);
                return result;
            }
            
            return ble_manager->write_message(message_buffer, message_length);
        },
        "VCSEC status poll"
    );
}

void PollingManager::request_infotainment_poll(bool bypass_delay) {
    ESP_LOGD(POLLING_MANAGER_TAG, "Requesting infotainment poll (bypass_delay=%s)", bypass_delay ? "true" : "false");
    
    // Check if we should delay this request due to recent commands
    if (!bypass_delay) {
        auto* state_manager = parent_->get_state_manager();
        if (state_manager && state_manager->should_delay_infotainment_request()) {
            ESP_LOGD(POLLING_MANAGER_TAG, "Delaying infotainment poll due to recent command");
            return;
        }
    }
    
    auto* command_manager = parent_->get_command_manager();
    if (!command_manager) {
        ESP_LOGE(POLLING_MANAGER_TAG, "Command manager not available");
        return;
    }
    
    command_manager->enqueue_command(
        UniversalMessage_Domain_DOMAIN_INFOTAINMENT,
        [this]() {
            auto* session_manager = parent_->get_session_manager();
            auto* ble_manager = parent_->get_ble_manager();
            
            if (!session_manager || !ble_manager) {
                ESP_LOGE(POLLING_MANAGER_TAG, "Required managers not available");
                return -1;
            }
            
            auto* client = session_manager->get_client();
            if (!client) {
                ESP_LOGE(POLLING_MANAGER_TAG, "Tesla client not available");
                return -1;
            }
            
            unsigned char message_buffer[1024];
            size_t message_length = 0;
            
            // Request charging data
            int result = client->buildCarServerGetVehicleDataMessage(
                message_buffer, &message_length,
                CarServer_GetVehicleData_getChargeState_tag);
            
            if (result != 0) {
                ESP_LOGE(POLLING_MANAGER_TAG, "Failed to build charging data request: %d", result);
                return result;
            }
            
            return ble_manager->write_message(message_buffer, message_length);
        },
        "infotainment data poll | charging"
    );
}

void PollingManager::request_wake_and_poll() {
    ESP_LOGI(POLLING_MANAGER_TAG, "Wake and poll requested");
    
    auto* command_manager = parent_->get_command_manager();
    if (!command_manager) {
        ESP_LOGE(POLLING_MANAGER_TAG, "Command manager not available");
        return;
    }
    
    // First wake the vehicle
    command_manager->enqueue_command(
        UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY,
        [this]() {
            auto* session_manager = parent_->get_session_manager();
            auto* ble_manager = parent_->get_ble_manager();
            
            if (!session_manager || !ble_manager) {
                return -1;
            }
            
            auto* client = session_manager->get_client();
            if (!client) {
                return -1;
            }
            
            unsigned char message_buffer[1024];
            size_t message_length = 0;
            
            int result = client->buildVCSECActionMessage(
                VCSEC_RKEAction_E_RKE_ACTION_WAKE_VEHICLE,
                message_buffer,
                &message_length);
            
            if (result != 0) {
                return result;
            }
            
            return ble_manager->write_message(message_buffer, message_length);
        },
        "wake vehicle"
    );
    
    // Then request VCSEC poll to get updated status
    command_manager->enqueue_command(
        UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY,
        [this]() {
            auto* session_manager = parent_->get_session_manager();
            auto* ble_manager = parent_->get_ble_manager();
            
            if (!session_manager || !ble_manager) {
                return -1;
            }
            
            auto* client = session_manager->get_client();
            if (!client) {
                return -1;
            }
            
            unsigned char message_buffer[1024];
            size_t message_length = 0;
            
            int result = client->buildVCSECInformationRequestMessage(
                VCSEC_InformationRequestType_INFORMATION_REQUEST_TYPE_GET_STATUS,
                message_buffer,
                &message_length);
            
            if (result != 0) {
                return result;
            }
            
            return ble_manager->write_message(message_buffer, message_length);
        },
        "data update after wake"
    );
}

void PollingManager::force_infotainment_poll() {
    ESP_LOGI(POLLING_MANAGER_TAG, "Force infotainment poll requested (bypassing delay)");
    request_infotainment_poll(true);
}

void PollingManager::force_full_update() {
    ESP_LOGI(POLLING_MANAGER_TAG, "Force full update requested (no wake command)");
    
    // Request fresh VCSEC data first (to get current sleep/awake status)
    request_vcsec_poll();
    
    // Then request infotainment data (bypassing any delays)
    force_infotainment_poll();
}

uint32_t PollingManager::time_since_last_vcsec_poll() const {
    if (last_vcsec_poll_ == 0) {
        return UINT32_MAX;
    }
    return millis() - last_vcsec_poll_;
}

uint32_t PollingManager::time_since_last_infotainment_poll() const {
    if (last_infotainment_poll_ == 0) {
        return UINT32_MAX;
    }
    return millis() - last_infotainment_poll_;
}

void PollingManager::log_polling_decision(const std::string& action, const std::string& reason) {
    ESP_LOGD(POLLING_MANAGER_TAG, "Polling decision: %s (reason: %s)", action.c_str(), reason.c_str());
}

} // namespace tesla_ble_vehicle
} // namespace esphome
