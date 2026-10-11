#pragma once

#include "adapters.h"
#include "write_retry_policy.h"
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

    BLETXChunk(std::vector<uint8_t> d, esp_gatt_write_type_t wt, esp_gatt_auth_req_t ar)
        : data(std::move(d)), write_type(wt), auth_req(ar) {}
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
    WriteRetryPolicy write_retry_policy_;
    
    // Bound on queued TX chunks (18 bytes each). A sick link previously
    // accumulated every accepted frame and protocol resend copy without limit;
    // whole frames are rejected once the backlog would exceed this, so memory
    // stays bounded and commands fail fast instead of queueing forever.
    // One maximum-size frame is 42 chunks; 256 holds large poll bursts.
    static const size_t MAX_TX_BACKLOG_CHUNKS = 256;
    static const size_t BLOCK_LENGTH = 18; // Safe BLE MTU chunk size
};

} // namespace tesla_ble_vehicle
} // namespace esphome
