#pragma once

#include <functional>
#include <queue>
#include <memory>
#include <esphome/core/log.h>
#include <universal_message.pb.h>
#include <vcsec.pb.h>
#include <car_server.pb.h>

namespace esphome {
namespace tesla_ble_vehicle {

static const char *const MESSAGE_HANDLER_TAG = "tesla_message_handler";

// Forward declarations
class TeslaBLEVehicle;

/**
 * @brief Message handler for processing Tesla BLE messages
 * 
 * This class handles parsing and processing of various message types from the vehicle,
 * including VCSEC and CarServer responses.
 */
class MessageHandler {
public:
    explicit MessageHandler(TeslaBLEVehicle* parent);
    
    // Main message processing
    void process_response_queue();
    void handle_universal_message(const UniversalMessage_RoutableMessage& message);
    
    // Specific message handlers
    void handle_vcsec_message(const UniversalMessage_RoutableMessage& message);
    void handle_carserver_message(const UniversalMessage_RoutableMessage& message);
    void handle_session_info(const UniversalMessage_RoutableMessage& message, UniversalMessage_Domain domain);
    
    // Vehicle status processing
    void handle_vehicle_status(const VCSEC_VehicleStatus& status);
    void handle_carserver_response(const CarServer_Response& response);
    void process_vehicle_data(const CarServer_VehicleData& vehicle_data);
    void update_command_state_on_response_with_status(const VCSEC_VehicleStatus& status);
    
    // Queue management
    void add_response(const UniversalMessage_RoutableMessage& message);
    size_t get_queue_size() const { return response_queue_.size(); }
    
private:
    TeslaBLEVehicle* parent_;
    std::queue<UniversalMessage_RoutableMessage> response_queue_;
    
    // Helper methods
    bool validate_message(const UniversalMessage_RoutableMessage& message);
    void log_message_details(const UniversalMessage_RoutableMessage& message);
    void update_command_state_on_response(const UniversalMessage_RoutableMessage& message);
};

} // namespace tesla_ble_vehicle
} // namespace esphome
