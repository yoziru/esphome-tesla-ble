#pragma once

#include <functional>
#include <queue>
#include <string>
#include <esphome/core/log.h>
#include <universal_message.pb.h>

namespace esphome {
namespace tesla_ble_vehicle {

static const char *const COMMAND_MANAGER_TAG = "tesla_command_manager";

// Forward declarations
class TeslaBLEVehicle;

enum class BLECommandState {
    IDLE,
    WAITING_FOR_VCSEC_AUTH,
    WAITING_FOR_VCSEC_AUTH_RESPONSE,
    WAITING_FOR_INFOTAINMENT_AUTH,
    WAITING_FOR_INFOTAINMENT_AUTH_RESPONSE,
    WAITING_FOR_WAKE,
    WAITING_FOR_WAKE_RESPONSE,
    READY,
    WAITING_FOR_RESPONSE,
};

struct BLECommand {
    UniversalMessage_Domain domain;
    std::function<int()> execute;
    std::string execute_name;
    BLECommandState state;
    uint32_t started_at;
    uint32_t last_tx_at;
    uint8_t retry_count;

    BLECommand(UniversalMessage_Domain d, std::function<int()> e, const std::string& n = "");
};

/**
 * @brief Command manager for Tesla BLE operations
 * 
 * This class manages the command queue, handles authentication states,
 * and ensures proper sequencing of BLE operations.
 */
class CommandManager {
public:
    static constexpr uint32_t COMMAND_TIMEOUT = 30 * 1000; // 30s
    static constexpr uint32_t MAX_LATENCY = 4 * 1000;      // 4s
    static constexpr uint8_t MAX_RETRIES = 5;
    static constexpr size_t MAX_QUEUE_SIZE = 20;           // Prevent unbounded queue growth
    
    explicit CommandManager(TeslaBLEVehicle* parent);
    
    // Command queue management
    void enqueue_command(UniversalMessage_Domain domain, std::function<int()> execute, const std::string& name);
    void process_command_queue();
    void clear_queue();
    
    // Command state management
    void update_command_state(BLECommandState new_state);
    void mark_command_completed();
    void mark_command_failed(const std::string& reason);
    
    // Authentication helpers
    bool is_domain_authenticated(UniversalMessage_Domain domain);
    void handle_authentication_response(UniversalMessage_Domain domain, bool success);
    
    // Queue inspection
    bool has_pending_commands() const { return !command_queue_.empty(); }
    size_t get_queue_size() const { return command_queue_.size(); }
    BLECommand* get_current_command();
    
    // Simple command creation helpers
    void enqueue_wake_vehicle();
    void enqueue_vcsec_poll();
    void enqueue_infotainment_poll();
    void enqueue_set_charging_state(bool enable);
    void enqueue_set_charging_amps(int amps);
    void enqueue_set_charging_limit(int limit);
    
private:
    TeslaBLEVehicle* parent_;
    std::queue<BLECommand> command_queue_;
    
    // Command processing helpers
    void process_idle_command(BLECommand& command);
    void process_auth_waiting_command(BLECommand& command);
    void process_ready_command(BLECommand& command);
    void handle_command_timeout(BLECommand& command);
    void retry_command(BLECommand& command);
    
    // Authentication state helpers
    void initiate_vcsec_auth(BLECommand& command);
    void initiate_infotainment_auth(BLECommand& command);
    void initiate_wake_sequence(BLECommand& command);
    
    // Counter management
    void increment_counter_for_command(BLECommand& command, UniversalMessage_Domain domain);
};

} // namespace tesla_ble_vehicle
} // namespace esphome
