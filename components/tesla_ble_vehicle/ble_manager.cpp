#include "ble_manager.h"
#include "tesla_ble_vehicle.h"
#include "common.h"
#include <client.h>
#include <logging.h>
#include <esphome/core/helpers.h>
#include <algorithm>

namespace esphome {
namespace tesla_ble_vehicle {

BLETXChunk::BLETXChunk(std::vector<unsigned char> d, esp_gatt_write_type_t wt, esp_gatt_auth_req_t ar)
    : data(std::move(d)), write_type(wt), auth_req(ar), sent_at(millis()), retry_count(0) {}

BLERXChunk::BLERXChunk(std::vector<unsigned char> b)
    : buffer(std::move(b)), received_at(millis()) {}

BLEManager::BLEManager(TeslaBLEVehicle* parent)
    : parent_(parent) {
    read_buffer_.reserve(MAX_BLE_MESSAGE_SIZE);
}

int BLEManager::write_message(const unsigned char* message_buffer, size_t message_length,
                             esp_gatt_write_type_t write_type, esp_gatt_auth_req_t auth_req) {
    if (message_buffer == nullptr || message_length == 0) {
        ESP_LOGE(BLE_MANAGER_TAG, "Invalid message parameters");
        return -1;
    }

    if (message_length > MAX_BLE_MESSAGE_SIZE) {
        ESP_LOGE(BLE_MANAGER_TAG, "Message too large: %zu bytes (max: %zu)", message_length, MAX_BLE_MESSAGE_SIZE);
        return -1;
    }

    ESP_LOGD(BLE_MANAGER_TAG, "BLE TX: %s", 
             TeslaBLE::format_hex(message_buffer, message_length).c_str());

    fragment_message(message_buffer, message_length, write_type, auth_req);
    
    ESP_LOGD(BLE_MANAGER_TAG, "Message fragmented into %zu chunks", write_queue_.size());
    return 0;
}

void BLEManager::fragment_message(const unsigned char* message, size_t length,
                                 esp_gatt_write_type_t write_type, esp_gatt_auth_req_t auth_req) {
    // Split message into BLE-sized chunks
    ESP_LOGD(BLE_MANAGER_TAG, "Fragmenting %zu byte message into %zu-byte chunks", 
             length, static_cast<size_t>(BLOCK_LENGTH));
    
    for (size_t i = 0; i < length; i += BLOCK_LENGTH) {
        size_t chunk_length = std::min(static_cast<size_t>(BLOCK_LENGTH), length - i);
        std::vector<unsigned char> chunk(message + i, message + i + chunk_length);
        
        ESP_LOGV(BLE_MANAGER_TAG, "BLE TX chunk %zu/%zu (%zu bytes): %s", 
                 (i / BLOCK_LENGTH) + 1, 
                 (length + BLOCK_LENGTH - 1) / BLOCK_LENGTH,
                 chunk_length,
                 TeslaBLE::format_hex(chunk.data(), chunk.size()).c_str());
        
        write_queue_.emplace(std::move(chunk), write_type, auth_req);
    }
}

void BLEManager::process_write_queue() {
    if (write_queue_.empty()) {
        return;
    }

    if (!parent_->is_connected()) {
        ESP_LOGW(BLE_MANAGER_TAG, "Cannot send data - BLE not connected");
        return;
    }

    BLETXChunk& chunk = write_queue_.front();
    
    int gattc_if = parent_->parent()->get_gattc_if();
    uint16_t conn_id = parent_->parent()->get_conn_id();
    uint16_t write_handle = parent_->get_write_handle();
    
    esp_err_t err = esp_ble_gattc_write_char(
        gattc_if, conn_id, write_handle, 
        chunk.data.size(), chunk.data.data(), 
        chunk.write_type, chunk.auth_req
    );
    
    if (err != ESP_OK) {
        ESP_LOGW(BLE_MANAGER_TAG, "Failed to send BLE write: %s", esp_err_to_name(err));
        handle_write_error("BLE write failed");
    } else {
        ESP_LOGV(BLE_MANAGER_TAG, "BLE TX chunk: %s", 
                 TeslaBLE::format_hex(chunk.data.data(), chunk.data.size()).c_str());
        write_queue_.pop();
    }
}

void BLEManager::add_received_data(const std::vector<unsigned char>& data) {
    if (data.empty()) {
        ESP_LOGW(BLE_MANAGER_TAG, "Received empty data chunk");
        return;
    }

    ESP_LOGV(BLE_MANAGER_TAG, "BLE RX chunk: %s", 
             TeslaBLE::format_hex(data.data(), data.size()).c_str());

    read_queue_.emplace(data);
}

void BLEManager::process_read_queue() {
    if (read_queue_.empty()) {
        return;
    }

    ESP_LOGV(BLE_MANAGER_TAG, "Processing BLE read queue (size: %zu)", read_queue_.size());
    
    BLERXChunk chunk = read_queue_.front();
    read_queue_.pop();

    // Check for buffer overflow before appending
    size_t new_size = read_buffer_.size() + chunk.buffer.size();
    if (new_size > MAX_BLE_MESSAGE_SIZE) {
        ESP_LOGE(BLE_MANAGER_TAG, "Message size would exceed maximum (%zu > %zu bytes), discarding message", 
                 new_size, MAX_BLE_MESSAGE_SIZE);
        clear_read_buffer();  // Immediately clear buffer to prevent corruption
        return;
    }

    // Append new data to buffer
    read_buffer_.insert(read_buffer_.end(), chunk.buffer.begin(), chunk.buffer.end());
    
    ESP_LOGV(BLE_MANAGER_TAG, "Read buffer now contains %zu bytes", read_buffer_.size());

    // Check if we have a complete message
    if (is_message_complete()) {
        process_complete_message();
    } else {
        // Show buffering progress if we have at least the length header
        if (read_buffer_.size() >= 2) {
            int expected_length = get_expected_message_length();
            ESP_LOGD(BLE_MANAGER_TAG, "BLE RX: Buffered chunk, waiting for more data.. (%zu/%d)", 
                     read_buffer_.size(), expected_length + 2);
        }
    }
}

bool BLEManager::is_message_complete() {
    if (read_buffer_.size() < 2) {
        ESP_LOGD(BLE_MANAGER_TAG, "BLE RX: Not enough data to determine message length");
        return false;
    }

    int expected_length = get_expected_message_length();
    if (expected_length < 0) {
        ESP_LOGW(BLE_MANAGER_TAG, "Invalid message length indicator");
        handle_read_error("Invalid message length");
        return false;
    }

    bool complete = read_buffer_.size() >= static_cast<size_t>(expected_length + 2);
    ESP_LOGV(BLE_MANAGER_TAG, "Message completeness check: %zu >= %d = %s", 
             read_buffer_.size(), expected_length + 2, complete ? "complete" : "incomplete");
    
    return complete;
}

int BLEManager::get_expected_message_length() {
    if (read_buffer_.size() < 2) {
        return -1;
    }
    
    // First two bytes contain the message length in big-endian format
    int length = (read_buffer_[0] << 8) | read_buffer_[1];
    
    // Validate that the length is reasonable (not too large)
    if (length > MAX_BLE_MESSAGE_SIZE - 2) {
        ESP_LOGW(BLE_MANAGER_TAG, "Invalid message length: %d (must be 0-%zu)", 
                 length, MAX_BLE_MESSAGE_SIZE - 2);
        return -1;
    }
    
    return length;
}

void BLEManager::process_complete_message() {
    ESP_LOGD(BLE_MANAGER_TAG, "BLE RX: %s", TeslaBLE::format_hex(read_buffer_.data(), read_buffer_.size()).c_str());
    ESP_LOGD(BLE_MANAGER_TAG, "Processing complete received message (%zu bytes)", read_buffer_.size());
    
    // Pass the complete message to the message handler
    if (parent_->get_message_handler()) {
        // Parse the message using the Tesla BLE client
        UniversalMessage_RoutableMessage message = UniversalMessage_RoutableMessage_init_default;
        
        auto* tesla_client = parent_->get_session_manager()->get_client();
        if (tesla_client) {
            int result = tesla_client->parseUniversalMessageBLE(
                read_buffer_.data(), read_buffer_.size(), &message);
            
            if (result == 0) {
                ESP_LOGD(BLE_MANAGER_TAG, "Successfully parsed universal message");
                parent_->get_message_handler()->add_response(message);
            } else {
                ESP_LOGE(BLE_MANAGER_TAG, "Failed to parse universal message (error: %d)", result);
                handle_read_error("Message parsing failed");
            }
        } else {
            ESP_LOGE(BLE_MANAGER_TAG, "Tesla client not available for message parsing");
            handle_read_error("Tesla client unavailable");
        }
    }

    // Clear the buffer for the next message
    clear_read_buffer();
}

void BLEManager::clear_read_buffer() {
    read_buffer_.clear();
    // Only shrink if buffer is significantly over-allocated to reduce memory churn
    if (read_buffer_.capacity() > MAX_BLE_MESSAGE_SIZE * 2) {
        read_buffer_.shrink_to_fit();
        ESP_LOGD(BLE_MANAGER_TAG, "Shrunk read buffer capacity to reduce memory usage");
    }
}

void BLEManager::clear_queues() {
    // Clear write queue
    std::queue<BLETXChunk> empty_write;
    write_queue_.swap(empty_write);
    
    // Clear read queue
    std::queue<BLERXChunk> empty_read;
    read_queue_.swap(empty_read);
    
    // Clear read buffer
    clear_read_buffer();
    
    ESP_LOGD(BLE_MANAGER_TAG, "All queues and buffers cleared");
}

void BLEManager::handle_read_error(const std::string& error_msg) {
    ESP_LOGW(BLE_MANAGER_TAG, "Read error: %s", error_msg.c_str());
    clear_read_buffer();
    
    // Optionally clear the read queue on errors to prevent cascading issues
    std::queue<BLERXChunk> empty_queue;
    read_queue_.swap(empty_queue);
}

void BLEManager::handle_write_error(const std::string& error_msg) {
    ESP_LOGW(BLE_MANAGER_TAG, "Write error: %s", error_msg.c_str());
    
    // For write errors, we might want to retry or clear the queue depending on the error
    // For now, just log the error - the queue will be processed again on the next loop
}

} // namespace tesla_ble_vehicle
} // namespace esphome
