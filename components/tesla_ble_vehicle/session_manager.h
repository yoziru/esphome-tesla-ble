#pragma once

#include <memory>
#include <vector>
#include <nvs_flash.h>
#include <esphome/core/log.h>
#include <universal_message.pb.h>
#include <signatures.pb.h>
#include "common.h"

namespace TeslaBLE {
    class Client;
}

namespace esphome {
namespace tesla_ble_vehicle {

static const char *const SESSION_MANAGER_TAG = "tesla_session_manager";

// Forward declarations
class TeslaBLEVehicle;

/**
 * @brief Session and authentication manager for Tesla BLE
 * 
 * This class handles session management, private key storage, and authentication
 * with the Tesla vehicle across different domains (VCSEC and Infotainment).
 */
class SessionManager {
public:
    static constexpr size_t PRIVATE_KEY_SIZE = 228;
    static constexpr size_t PUBLIC_KEY_SIZE = 65;
    static const char* NVS_KEY_INFOTAINMENT;
    static const char* NVS_KEY_VCSEC;
    static const char* NVS_KEY_PRIVATE_KEY;
    
    explicit SessionManager(TeslaBLEVehicle* parent);
    ~SessionManager();
    
    // Initialization
    bool initialize();
    void cleanup();
    
    // Key management
    bool create_private_key();
    bool load_private_key();
    bool regenerate_key();
    bool get_public_key(unsigned char* buffer, size_t* length);
    
    // Session management
    bool load_session_info(UniversalMessage_Domain domain);
    bool save_session_info(const Signatures_SessionInfo& session_info, UniversalMessage_Domain domain);
    bool update_session(const Signatures_SessionInfo& session_info, UniversalMessage_Domain domain);
    void invalidate_session(UniversalMessage_Domain domain);
    
    // Authentication state
    bool is_domain_authenticated(UniversalMessage_Domain domain);
    bool request_session_info(UniversalMessage_Domain domain);
    
    // Vehicle pairing
    bool start_pairing(const std::string& role);
    
    // Getters
    TeslaBLE::Client* get_client() const { return tesla_client_.get(); }
    
private:
    TeslaBLEVehicle* parent_;
    std::unique_ptr<TeslaBLE::Client> tesla_client_;
    nvs_handle_t storage_handle_;
    bool initialized_;
    
    // NVS operations
    bool initialize_nvs();
    bool load_from_nvs(const char* key, std::vector<uint8_t>& data);
    bool save_to_nvs(const char* key, const void* data, size_t size);
    
    // Helper methods
    const char* get_nvs_key_for_domain(UniversalMessage_Domain domain);
    void log_session_info(const Signatures_SessionInfo& session_info);
    bool encode_session_info(const Signatures_SessionInfo& session_info, std::vector<uint8_t>& encoded);
    bool decode_session_info(const std::vector<uint8_t>& encoded, Signatures_SessionInfo& session_info);
};

} // namespace tesla_ble_vehicle
} // namespace esphome
