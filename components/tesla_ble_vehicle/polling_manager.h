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
    // Default polling intervals (in milliseconds) - can be overridden
    static constexpr uint32_t DEFAULT_VCSEC_POLL_INTERVAL = 10000;                // 10s - safe for asleep vehicle
    static constexpr uint32_t DEFAULT_INFOTAINMENT_POLL_INTERVAL_AWAKE = 30000;   // 30s - when awake but not active
    static constexpr uint32_t DEFAULT_INFOTAINMENT_POLL_INTERVAL_ACTIVE = 10000;  // 10s - when charging or unlocked
    static constexpr uint32_t INITIAL_CONNECTION_DELAY = 10000;                   // 10s - delay after connection
    
    explicit PollingManager(TeslaBLEVehicle* parent);
    
    // Configuration setters
    void set_vcsec_poll_interval(uint32_t interval_ms) { vcsec_poll_interval_ = interval_ms; }
    void set_infotainment_poll_interval_awake(uint32_t interval_ms) { infotainment_poll_interval_awake_ = interval_ms; }
    void set_infotainment_poll_interval_active(uint32_t interval_ms) { infotainment_poll_interval_active_ = interval_ms; }
    void set_infotainment_sleep_timeout(uint32_t interval_ms) { infotainment_sleep_timeout_ = interval_ms; }
    
    // Configuration getters
    uint32_t get_vcsec_poll_interval() const { return vcsec_poll_interval_; }
    uint32_t get_infotainment_poll_interval_awake() const { return infotainment_poll_interval_awake_; }
    uint32_t get_infotainment_poll_interval_active() const { return infotainment_poll_interval_active_; }
    uint32_t get_infotainment_sleep_timeout() const { return infotainment_sleep_timeout_; }
    
    // Main polling logic
    void update();
    void handle_connection_established();
    void handle_connection_lost();
    
    // State tracking
    void update_vehicle_state(bool is_awake, bool is_charging, bool is_unlocked, bool is_user_present);
    void force_immediate_poll();
    
    // Polling decisions
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
    
    // Configurable polling intervals (in milliseconds)
    uint32_t vcsec_poll_interval_{DEFAULT_VCSEC_POLL_INTERVAL};
    uint32_t infotainment_poll_interval_awake_{DEFAULT_INFOTAINMENT_POLL_INTERVAL_AWAKE};
    uint32_t infotainment_poll_interval_active_{DEFAULT_INFOTAINMENT_POLL_INTERVAL_ACTIVE};
    uint32_t infotainment_sleep_timeout_{660000}; // 11 minutes default
    
    // Timing state
    uint32_t last_vcsec_poll_{0};
    uint32_t last_infotainment_poll_{0};
    uint32_t connection_time_{0};
    uint32_t wake_time_{0};  // When vehicle last woke up
    bool just_connected_{false};
    
    // Vehicle state cache
    bool was_awake_{false};
    bool was_charging_{false};
    bool was_unlocked_{false};
    bool was_user_present_{false};
    
    // Helper methods
    uint32_t get_infotainment_poll_interval();
    bool should_use_fast_polling();
    std::string get_fast_poll_reason();
    void log_polling_decision(const std::string& action, const std::string& reason);
    
    // Rollover-safe time calculations
    uint32_t time_since(uint32_t timestamp) const;
    bool has_elapsed(uint32_t timestamp, uint32_t interval) const;
};

} // namespace tesla_ble_vehicle
} // namespace esphome
