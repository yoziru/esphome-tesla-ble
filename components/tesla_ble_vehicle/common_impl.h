#pragma once

#include "common.h"
#include "tesla_ble_vehicle.h"
#include "session_manager.h"  
#include "ble_manager.h"

namespace esphome {
namespace tesla_ble_vehicle {

template<typename BuilderFunc>
std::function<int()> BLECommandHelper::create_command(TeslaBLEVehicle* vehicle, BuilderFunc builder) {
    return [vehicle, builder]() {
        auto* session_manager = vehicle->get_session_manager();
        auto* ble_manager = vehicle->get_ble_manager();
        
        if (!session_manager || !ble_manager) {
            return -1;
        }
        
        auto* client = session_manager->get_client();
        if (!client) {
            return -1;
        }
        
        unsigned char message_buffer[MAX_BLE_MESSAGE_SIZE];
        size_t message_length = 0;
        
        int result = builder(client, message_buffer, &message_length);
        if (result != 0) {
            return result;
        }
        
        return ble_manager->write_message(message_buffer, message_length);
    };
}

template<typename BuilderFunc>
std::function<int()> BLECommandHelper::create_command(SessionManager* session_manager, 
                                                      BLEManager* ble_manager, 
                                                      BuilderFunc builder) {
    return [session_manager, ble_manager, builder]() {
        if (!session_manager || !ble_manager) {
            return -1;
        }
        
        auto* client = session_manager->get_client();
        if (!client) {
            return -1;
        }
        
        unsigned char message_buffer[MAX_BLE_MESSAGE_SIZE];
        size_t message_length = 0;
        
        int result = builder(client, message_buffer, &message_length);
        if (result != 0) {
            return result;
        }
        
        return ble_manager->write_message(message_buffer, message_length);
    };
}

} // namespace tesla_ble_vehicle
} // namespace esphome
