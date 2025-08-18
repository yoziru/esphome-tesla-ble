#pragma once

#include <esphome/core/log.h>

namespace esphome {
namespace tesla_ble_vehicle {

static const char *const POLLING_MANAGER_TAG = "tesla_polling_manager";

// Forward declarations
class TeslaBLEVehicle;

/**
 * @brief Smart polling manager for Tesla BLE
 * 
 * This class implements intelligent polling strategies to minimize battery drain
 * while ensuring timely updates based on vehicle state.
 */
class PollingManager {
public:
    // Polling intervals (in milliseconds)
    static constexpr uint32_t VCSEC_POLL_INTERVAL = 10000;        // 10s - safe for asleep vehicle
    static constexpr uint32_t INFOTAINMENT_POLL_AWAKE = 30000;    // 30s - when awake but not active
    static constexpr uint32_t INFOTAINMENT_POLL_CHARGING = 10000; // 10s - when charging or unlocked
    static constexpr uint32_t INITIAL_CONNECTION_DELAY = 10000;   // 10s - delay after connection
    
    explicit PollingManager(TeslaBLEVehicle* parent);
    
    // Main polling logic
    void update();
    void handle_connection_established();
    void handle_connection_lost();
    
    // State tracking
    void update_vehicle_state(bool is_awake, bool is_charging, bool is_unlocked);
    void force_immediate_poll();
    
    // Polling decisions
    bool should_poll_vcsec();
    bool should_poll_infotainment();
    
    // Manual polling triggers
    void request_vcsec_poll();
    void request_infotainment_poll(bool bypass_delay = false);
    void request_wake_and_poll();
    void force_infotainment_poll(); // Always bypasses delay for user-requested updates
    void force_full_update(); // Requests fresh data without sending wake command
    
    // State queries
    bool just_connected() const { return just_connected_; }
    uint32_t time_since_last_vcsec_poll() const;
    uint32_t time_since_last_infotainment_poll() const;
    
private:
    TeslaBLEVehicle* parent_;
    
    // Timing state
    uint32_t last_vcsec_poll_{0};
    uint32_t last_infotainment_poll_{0};
    uint32_t connection_time_{0};
    bool just_connected_{false};
    
    // Vehicle state cache
    bool is_awake_{false};
    bool is_charging_{false};
    bool is_unlocked_{false};
    
    // Helper methods
    uint32_t get_infotainment_poll_interval();
    bool should_use_fast_polling();
    std::string get_fast_poll_reason();
    void log_polling_decision(const std::string& action, const std::string& reason);
};

} // namespace tesla_ble_vehicle
} // namespace esphome
