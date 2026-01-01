#pragma once

#include "adapters.h"
#include <vector>
#include <string>
#include <nvs_flash.h>
#include <nvs.h>

namespace esphome {
namespace tesla_ble_vehicle {

class StorageAdapterImpl : public ::TeslaBLE::StorageAdapter {
public:
    StorageAdapterImpl();
    ~StorageAdapterImpl();
    
    bool load(const std::string& key, std::vector<uint8_t>& buffer) override;
    bool save(const std::string& key, const std::vector<uint8_t>& buffer) override;
    bool remove(const std::string& key) override;
    
    // Initialize NVS
    bool initialize();

private:
    nvs_handle_t storage_handle_;
    bool initialized_;
    
    // Helper to map string keys to consistent NVS keys (since NVS keys are max 15 chars)
    // Actually SessionManager used fixed keys "tk_vcsec", "tk_infotainment".
    // TeslaBLE library passes "session_vcsec", "session_infotainment".
    // We can just map them directly or use a hash or use the library keys if short enough?
    // "session_vcsec" is 13 chars. "session_infotainment" is 20 chars (Too long!)
    // So we MUST map them.
    const char* map_key(const std::string& key);
};

} // namespace tesla_ble_vehicle
} // namespace esphome
