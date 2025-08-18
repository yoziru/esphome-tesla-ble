#pragma once

#include <functional>

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

} // namespace tesla_ble_vehicle
} // namespace esphome
