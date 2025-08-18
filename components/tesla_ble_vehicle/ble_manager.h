#pragma once

#include <queue>
#include <vector>
#include <esp_gattc_api.h>
#include <esphome/core/log.h>

namespace esphome {
namespace tesla_ble_vehicle {

static const char *const BLE_MANAGER_TAG = "tesla_ble_manager";

// Forward declarations
class TeslaBLEVehicle;

struct BLETXChunk {
    std::vector<unsigned char> data;
    esp_gatt_write_type_t write_type;
    esp_gatt_auth_req_t auth_req;
    uint32_t sent_at;
    uint8_t retry_count;

    BLETXChunk(std::vector<unsigned char> d, esp_gatt_write_type_t wt, esp_gatt_auth_req_t ar);
};

struct BLERXChunk {
    std::vector<unsigned char> buffer;
    uint32_t received_at;

    explicit BLERXChunk(std::vector<unsigned char> b);
};

/**
 * @brief BLE communication manager for Tesla vehicles
 * 
 * This class handles low-level BLE communication, including message fragmentation,
 * reassembly, and queue management.
 */
class BLEManager {
public:
    static constexpr int BLOCK_LENGTH = 20;           // BLE chunk size
    static constexpr int RX_TIMEOUT = 1000;           // 1s timeout between chunks
    
    explicit BLEManager(TeslaBLEVehicle* parent);
    
    // Message transmission
    int write_message(const unsigned char* message_buffer, size_t message_length,
                     esp_gatt_write_type_t write_type = ESP_GATT_WRITE_TYPE_NO_RSP,
                     esp_gatt_auth_req_t auth_req = ESP_GATT_AUTH_REQ_NONE);
    
    // Queue processing
    void process_write_queue();
    void process_read_queue();
    
    // Data reception
    void add_received_data(const std::vector<unsigned char>& data);
    
    // Queue management
    void clear_queues();
    size_t get_write_queue_size() const { return write_queue_.size(); }
    size_t get_read_queue_size() const { return read_queue_.size(); }
    
    // Buffer management
    void clear_read_buffer();
    size_t get_read_buffer_size() const { return read_buffer_.size(); }
    
private:
    TeslaBLEVehicle* parent_;
    std::queue<BLETXChunk> write_queue_;
    std::queue<BLERXChunk> read_queue_;
    std::vector<unsigned char> read_buffer_;
    
    // Helper methods
    bool is_message_complete();
    int get_expected_message_length();
    void process_complete_message();
    void fragment_message(const unsigned char* message, size_t length,
                         esp_gatt_write_type_t write_type, esp_gatt_auth_req_t auth_req);
    
    // Error handling
    void handle_read_error(const std::string& error_msg);
    void handle_write_error(const std::string& error_msg);
};

} // namespace tesla_ble_vehicle
} // namespace esphome
