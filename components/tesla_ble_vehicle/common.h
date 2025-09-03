#pragma once

#include <functional>
#include <esphome/core/helpers.h>
#include <esphome/core/hal.h>

namespace esphome {
namespace tesla_ble_vehicle {

// Common constants - shared across all components
static constexpr size_t MAX_BLE_MESSAGE_SIZE = 1024;

// Validation constants
static constexpr int MIN_CHARGING_AMPS = 0;
static constexpr int MAX_CHARGING_AMPS = 80;  // Theoretical max, actual max comes from vehicle
static constexpr int MIN_CHARGING_LIMIT = 50;
static constexpr int MAX_CHARGING_LIMIT = 100;

// Forward declarations
class TeslaBLEVehicle;
class SessionManager;
class BLEManager;

/**
 * @brief Utility functions for common operations
 */
class Utils {
public:
    /**
     * @brief Calculate time difference in milliseconds, handling millis() rollover
     * @param now Current time from millis()
     * @param timestamp Past timestamp to compare against
     * @return Time difference in milliseconds
     */
    static uint32_t time_since(uint32_t now, uint32_t timestamp) {
        return (now >= timestamp) ? (now - timestamp) : (UINT32_MAX - timestamp + now + 1);
    }
    
    /**
     * @brief Check if enough time has elapsed since a timestamp, handling rollover
     * @param timestamp Past timestamp to check against
     * @param interval Required interval in milliseconds
     * @return true if interval has elapsed
     */
    static bool has_elapsed(uint32_t timestamp, uint32_t interval) {
        return time_since(millis(), timestamp) >= interval;
    }
};

/**
 * @brief Generic helper for creating BLE commands with reduced code duplication
 * 
 * This template class provides a standardized way to create BLE command functions
 * that handle the common pattern of:
 * 1. Get client from session manager
 * 2. Create message buffer
 * 3. Build message using client
 * 4. Send via BLE manager
 * 
 * Note: Template implementations are in common_impl.h to avoid incomplete type issues
 */
class BLECommandHelper {
public:
    /**
     * @brief Create a BLE command function using a TeslaBLEVehicle instance
     * @param vehicle Pointer to the main vehicle component
     * @param builder Lambda that builds the message using client, buffer, and length
     * @return Function that can be passed to command_manager->enqueue_command()
     */
    template<typename BuilderFunc>
    static std::function<int()> create_command(TeslaBLEVehicle* vehicle, BuilderFunc builder);
    
    /**
     * @brief Create a BLE command function using explicit manager pointers
     * @param session_manager Pointer to session manager
     * @param ble_manager Pointer to BLE manager  
     * @param builder Lambda that builds the message using client, buffer, and length
     * @return Function that can be passed to command_manager->enqueue_command()
     */
    template<typename BuilderFunc>
    static std::function<int()> create_command(SessionManager* session_manager, 
                                               BLEManager* ble_manager, 
                                               BuilderFunc builder);
};

/**
 * @brief Logging helpers to reduce code duplication
 */
class LogHelper {
public:
    /**
     * @brief Log command timeout with standardized format
     */
    static void log_command_timeout(const char* tag, const std::string& command_name, 
                                   uint32_t timeout_ms, const char* context = "") {
        if (strlen(context) > 0) {
            ESP_LOGE(tag, "[%s] Command timed out %s after %d ms", 
                    command_name.c_str(), context, timeout_ms);
        } else {
            ESP_LOGE(tag, "[%s] Command timed out after %d ms", 
                    command_name.c_str(), timeout_ms);
        }
    }
    
    /**
     * @brief Log command retry with standardized format
     */
    static void log_command_retry(const char* tag, const std::string& command_name, 
                                 int attempt, int max_attempts, const char* reason = "") {
        if (strlen(reason) > 0) {
            ESP_LOGW(tag, "[%s] %s, retrying (attempt %d/%d)", 
                    command_name.c_str(), reason, attempt, max_attempts);
        } else {
            ESP_LOGI(tag, "[%s] Executing command (attempt %d/%d)", 
                    command_name.c_str(), attempt, max_attempts);
        }
    }
};

} // namespace tesla_ble_vehicle
} // namespace esphome
