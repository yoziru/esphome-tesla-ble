#include "ble_adapter_impl.h"
#include "storage_adapter_impl.h"
#include "tesla_ble_vehicle.h"
#include <esphome/core/log.h>
#include <tb_utils.h>
#include <algorithm>

namespace esphome {
namespace tesla_ble_vehicle {

static const char *ADAPTER_TAG = "tesla_ble_adapters";

// --- BleAdapterImpl ---

BleAdapterImpl::BleAdapterImpl(TeslaBLEVehicle* parent) : parent_(parent) {}

void BleAdapterImpl::connect(const std::string& address) {
    // ESPHome handles connection
}

void BleAdapterImpl::disconnect() {
    if (parent_) {
        parent_->parent()->disconnect();
    }
}

bool BleAdapterImpl::write(const std::vector<uint8_t>& data) {
    if (!parent_->is_connected()) return false;
    
    ESP_LOGD(ADAPTER_TAG, "BLE TX: %s", TeslaBLE::format_hex(data.data(), data.size()).c_str());
    
    // Fragment message
    for (size_t i = 0; i < data.size(); i += BLOCK_LENGTH) {
        size_t chunk_len = std::min(BLOCK_LENGTH, data.size() - i);
        std::vector<uint8_t> chunk(data.begin() + i, data.begin() + i + chunk_len);
        
        write_queue_.emplace(chunk, ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
    }
    
    return true;
}

void BleAdapterImpl::process_write_queue() {
    if (write_queue_.empty()) return;
    if (!parent_->is_connected()) return;
    
    // Rate limit? Or just send one per loop?
    // BLEManager sent one per loop (implied by just popping front)
    
    BLETXChunk& chunk = write_queue_.front();
    
    auto* client = parent_->parent();
    int gattc_if = client->get_gattc_if();
    uint16_t conn_id = client->get_conn_id();
    uint16_t handle = parent_->get_write_handle(); // Need public getter on Vehicle
    
    if (handle == 0) {
        // Not ready
        return;
    }
    
    esp_err_t err = esp_ble_gattc_write_char(
        gattc_if, conn_id, handle,
        chunk.data.size(), chunk.data.data(),
        chunk.write_type, chunk.auth_req
    );
    
    if (err == ESP_OK) {
        write_queue_.pop();
    } else {
        ESP_LOGW(ADAPTER_TAG, "BLE write failed: %s", esp_err_to_name(err));
        // Retry? 
    }
}

void BleAdapterImpl::clear_queues() {
    std::queue<BLETXChunk> empty;
    write_queue_.swap(empty);
}

// --- StorageAdapterImpl ---

StorageAdapterImpl::StorageAdapterImpl() : storage_handle_(0), initialized_(false) {}

StorageAdapterImpl::~StorageAdapterImpl() {
    if (storage_handle_ != 0) {
        nvs_close(storage_handle_);
    }
}

bool StorageAdapterImpl::initialize() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    
    if (err != ESP_OK) return false;
    
    err = nvs_open("storage", NVS_READWRITE, &storage_handle_);
    if (err != ESP_OK) return false;
    
    initialized_ = true;
    return true;
}

const char* StorageAdapterImpl::map_key(const std::string& key) {
    if (key == "session_vcsec") return "tk_vcsec";
    if (key == "session_infotainment") return "tk_infotainment";
    if (key == "private_key") return "private_key"; // Unchanged
    return nullptr;
}

bool StorageAdapterImpl::load(const std::string& key, std::vector<uint8_t>& buffer) {
    if (!initialized_) return false;
    
    const char* nvs_key = map_key(key);
    if (!nvs_key) return false;
    
    size_t required_size = 0;
    esp_err_t err = nvs_get_blob(storage_handle_, nvs_key, nullptr, &required_size);
    if (err != ESP_OK || required_size == 0) return false;
    
    buffer.resize(required_size);
    err = nvs_get_blob(storage_handle_, nvs_key, buffer.data(), &required_size);
    return err == ESP_OK;
}

bool StorageAdapterImpl::save(const std::string& key, const std::vector<uint8_t>& buffer) {
    if (!initialized_) return false;
    
    const char* nvs_key = map_key(key);
    if (!nvs_key) return false;
    
    esp_err_t err = nvs_set_blob(storage_handle_, nvs_key, buffer.data(), buffer.size());
    if (err != ESP_OK) return false;
    
    return nvs_commit(storage_handle_) == ESP_OK;
}

bool StorageAdapterImpl::remove(const std::string& key) {
    if (!initialized_) return false;
    
    const char* nvs_key = map_key(key);
    if (!nvs_key) return false;
    
    esp_err_t err = nvs_erase_key(storage_handle_, nvs_key);
    return (err == ESP_OK) && (nvs_commit(storage_handle_) == ESP_OK);
}

} // namespace tesla_ble_vehicle
} // namespace esphome
