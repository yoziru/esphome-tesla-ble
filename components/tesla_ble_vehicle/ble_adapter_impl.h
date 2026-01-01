#pragma once

#include "adapters.h"
#include <esphome/components/ble_client/ble_client.h>
#include <esphome/core/log.h>
#include <vector>
#include <queue>
#include <esp_gattc_api.h>

namespace esphome {
namespace tesla_ble_vehicle {

class TeslaBLEVehicle; // Forward declaration

struct BLETXChunk {
    std::vector<uint8_t> data;
    esp_gatt_write_type_t write_type;
    esp_gatt_auth_req_t auth_req;
    uint32_t sent_at;
    
    BLETXChunk(std::vector<uint8_t> d, esp_gatt_write_type_t wt, esp_gatt_auth_req_t ar)
        : data(std::move(d)), write_type(wt), auth_req(ar), sent_at(millis()) {}
};

class BleAdapterImpl : public TeslaBLE::BleAdapter {
public:
    explicit BleAdapterImpl(TeslaBLEVehicle* parent);

    void connect(const std::string& address) override;
    void disconnect() override;
    bool write(const std::vector<uint8_t>& data) override;

    // Custom method to be called by TeslaBLEVehicle loop
    void process_write_queue();
    
    // Clear queues (on disconnect)
    void clear_queues();

private:
    TeslaBLEVehicle* parent_;
    std::queue<BLETXChunk> write_queue_;
    
    static const size_t BLOCK_LENGTH = 18; // Safe BLE MTU chunk size
};

} // namespace tesla_ble_vehicle
} // namespace esphome
