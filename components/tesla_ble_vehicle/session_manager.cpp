#include "session_manager.h"
#include "tesla_ble_vehicle.h"
#include <client.h>
#include <logging.h>
#include <keys.pb.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include <esphome/core/helpers.h>

namespace esphome {
namespace tesla_ble_vehicle {

// Static constants
const char* SessionManager::NVS_KEY_INFOTAINMENT = "tk_infotainment";
const char* SessionManager::NVS_KEY_VCSEC = "tk_vcsec";
const char* SessionManager::NVS_KEY_PRIVATE_KEY = "private_key";

SessionManager::SessionManager(TeslaBLEVehicle* parent)
    : parent_(parent), tesla_client_(std::make_unique<TeslaBLE::Client>()), 
      storage_handle_(0), initialized_(false) {}

SessionManager::~SessionManager() {
    cleanup();
}

bool SessionManager::initialize() {
    ESP_LOGD(SESSION_MANAGER_TAG, "Initializing session manager");
    
    if (!initialize_nvs()) {
        ESP_LOGE(SESSION_MANAGER_TAG, "Failed to initialize NVS");
        return false;
    }
    
    if (!load_private_key()) {
        ESP_LOGW(SESSION_MANAGER_TAG, "Failed to load private key, creating new one");
        if (!create_private_key()) {
            ESP_LOGE(SESSION_MANAGER_TAG, "Failed to create private key");
            return false;
        }
    }
    
    // Load existing session info for both domains
    load_session_info(UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY);
    load_session_info(UniversalMessage_Domain_DOMAIN_INFOTAINMENT);
    
    initialized_ = true;
    ESP_LOGI(SESSION_MANAGER_TAG, "Session manager initialized successfully");
    return true;
}

void SessionManager::cleanup() {
    if (storage_handle_ != 0) {
        nvs_close(storage_handle_);
        storage_handle_ = 0;
    }
    initialized_ = false;
}

bool SessionManager::initialize_nvs() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(SESSION_MANAGER_TAG, "NVS partition needs to be erased");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    
    if (err != ESP_OK) {
        ESP_LOGE(SESSION_MANAGER_TAG, "Failed to initialize NVS flash: %s", esp_err_to_name(err));
        return false;
    }
    
    err = nvs_open("storage", NVS_READWRITE, &storage_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(SESSION_MANAGER_TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
        return false;
    }
    
    return true;
}

bool SessionManager::create_private_key() {
    ESP_LOGI(SESSION_MANAGER_TAG, "Creating new private key");
    
    int result = tesla_client_->createPrivateKey();
    if (result != 0) {
        ESP_LOGE(SESSION_MANAGER_TAG, "Failed to create private key: %d", result);
        return false;
    }
    
    // Get the private key and save it
    unsigned char private_key_buffer[PRIVATE_KEY_SIZE];
    size_t private_key_length = 0;
    
    tesla_client_->getPrivateKey(private_key_buffer, sizeof(private_key_buffer), &private_key_length);
    
    if (!save_to_nvs(NVS_KEY_PRIVATE_KEY, private_key_buffer, private_key_length)) {
        ESP_LOGE(SESSION_MANAGER_TAG, "Failed to save private key to NVS");
        return false;
    }
    
    ESP_LOGI(SESSION_MANAGER_TAG, "Private key created and saved successfully");
    return true;
}

bool SessionManager::load_private_key() {
    ESP_LOGD(SESSION_MANAGER_TAG, "Loading private key from NVS");
    
    std::vector<uint8_t> private_key_data;
    if (!load_from_nvs(NVS_KEY_PRIVATE_KEY, private_key_data)) {
        ESP_LOGD(SESSION_MANAGER_TAG, "No existing private key found");
        return false;
    }
    
    if (private_key_data.size() != PRIVATE_KEY_SIZE) {
        ESP_LOGW(SESSION_MANAGER_TAG, "Invalid private key size: %zu (expected %zu)", 
                 private_key_data.size(), PRIVATE_KEY_SIZE);
        return false;
    }
    
    int result = tesla_client_->loadPrivateKey(private_key_data.data(), private_key_data.size());
    if (result != 0) {
        ESP_LOGE(SESSION_MANAGER_TAG, "Failed to load private key: %d", result);
        return false;
    }
    
    ESP_LOGI(SESSION_MANAGER_TAG, "Private key loaded successfully");
    return true;
}

bool SessionManager::regenerate_key() {
    ESP_LOGI(SESSION_MANAGER_TAG, "Regenerating private key");
    
    // Invalidate existing sessions
    invalidate_session(UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY);
    invalidate_session(UniversalMessage_Domain_DOMAIN_INFOTAINMENT);
    
    // Create new private key
    if (!create_private_key()) {
        ESP_LOGE(SESSION_MANAGER_TAG, "Failed to regenerate private key");
        return false;
    }
    
    ESP_LOGI(SESSION_MANAGER_TAG, "Private key regenerated successfully");
    return true;
}

bool SessionManager::get_public_key(unsigned char* buffer, size_t* length) {
    if (!tesla_client_ || !buffer || !length) {
        return false;
    }
    
    int result = tesla_client_->getPublicKey(buffer, length);
    return result == 0;
}

bool SessionManager::load_session_info(UniversalMessage_Domain domain) {
    ESP_LOGD(SESSION_MANAGER_TAG, "Loading session info for %s", TeslaBLE::domain_to_string(domain));
    
    const char* nvs_key = get_nvs_key_for_domain(domain);
    if (!nvs_key) {
        ESP_LOGE(SESSION_MANAGER_TAG, "Invalid domain for session loading");
        return false;
    }
    
    std::vector<uint8_t> session_data;
    if (!load_from_nvs(nvs_key, session_data)) {
        ESP_LOGD(SESSION_MANAGER_TAG, "No existing session info found for %s", TeslaBLE::domain_to_string(domain));
        return false;
    }
    
    Signatures_SessionInfo session_info = Signatures_SessionInfo_init_default;
    if (!decode_session_info(session_data, session_info)) {
        ESP_LOGE(SESSION_MANAGER_TAG, "Failed to decode session info for %s", TeslaBLE::domain_to_string(domain));
        return false;
    }
    
    log_session_info(session_info);
    
    // Update the Tesla client with the session info
    auto peer = tesla_client_->getPeer(domain);
    if (peer) {
        peer->updateSession(&session_info);
        ESP_LOGI(SESSION_MANAGER_TAG, "Session info loaded for %s", TeslaBLE::domain_to_string(domain));
        return true;
    }
    
    ESP_LOGE(SESSION_MANAGER_TAG, "Failed to get peer for domain %s", TeslaBLE::domain_to_string(domain));
    return false;
}

bool SessionManager::save_session_info(const Signatures_SessionInfo& session_info, UniversalMessage_Domain domain) {
    ESP_LOGD(SESSION_MANAGER_TAG, "Saving session info for %s", TeslaBLE::domain_to_string(domain));
    
    const char* nvs_key = get_nvs_key_for_domain(domain);
    if (!nvs_key) {
        ESP_LOGE(SESSION_MANAGER_TAG, "Invalid domain for session saving");
        return false;
    }
    
    std::vector<uint8_t> encoded_data;
    if (!encode_session_info(session_info, encoded_data)) {
        ESP_LOGE(SESSION_MANAGER_TAG, "Failed to encode session info for %s", TeslaBLE::domain_to_string(domain));
        return false;
    }
    
    if (!save_to_nvs(nvs_key, encoded_data.data(), encoded_data.size())) {
        ESP_LOGE(SESSION_MANAGER_TAG, "Failed to save session info to NVS for %s", TeslaBLE::domain_to_string(domain));
        return false;
    }
    
    ESP_LOGI(SESSION_MANAGER_TAG, "Session info saved for %s", TeslaBLE::domain_to_string(domain));
    return true;
}

int SessionManager::update_session(const Signatures_SessionInfo& session_info, UniversalMessage_Domain domain) {
    ESP_LOGD(SESSION_MANAGER_TAG, "Updating session for %s", TeslaBLE::domain_to_string(domain));
    
    // Get the peer to check current state
    auto peer = tesla_client_->getPeer(domain);
    if (!peer) {
        ESP_LOGE(SESSION_MANAGER_TAG, "Failed to get peer for domain %s", TeslaBLE::domain_to_string(domain));
        return -1;
    }
    
    // Log the counter comparison for debugging
    ESP_LOGD(SESSION_MANAGER_TAG, "Session info counter comparison for %s: current=%u, received=%u", 
             TeslaBLE::domain_to_string(domain), peer->getCounter(), session_info.counter);
    
    // Always try to update with the vehicle's session info first
    int result = peer->updateSession(const_cast<Signatures_SessionInfo*>(&session_info));
    
    if (result == 0) {
        // Successful update - save the session info
        ESP_LOGI(SESSION_MANAGER_TAG, "Successfully updated session for %s with counter %u", 
                 TeslaBLE::domain_to_string(domain), session_info.counter);
        if (!save_session_info(session_info, domain)) {
            ESP_LOGW(SESSION_MANAGER_TAG, "Failed to save updated session info for %s", TeslaBLE::domain_to_string(domain));
        }
        return 0;
    } else if (result == TeslaBLE::TeslaBLE_Status_E_ERROR_INVALID_SESSION || result == TeslaBLE::TeslaBLE_Status_E_ERROR_COUNTER_REPLAY) {
        // Counter anti-replay or rollback - the vehicle's session info is the authoritative truth
        // We need to force our session to match the vehicle's state
        ESP_LOGW(SESSION_MANAGER_TAG, "Counter anti-replay detected for %s, forcing session to match vehicle's authoritative state (vehicle counter: %u, our counter: %u)", 
                 TeslaBLE::domain_to_string(domain), session_info.counter, peer->getCounter());
        
        // Invalidate and erase stored session first
        invalidate_session(domain);
        
        // Force update peer state directly with vehicle's authoritative values
        peer->setCounter(session_info.counter);
        peer->setEpoch(session_info.epoch);
        peer->setTimeZero(std::time(nullptr) - session_info.clock_time);
        peer->setIsValid(true);
        
        // Load Tesla key if provided
        if (session_info.publicKey.size > 0) {
            peer->loadTeslaKey(session_info.publicKey.bytes, session_info.publicKey.size);
        }
        
        // Save the authoritative session info from the vehicle
        if (!save_session_info(session_info, domain)) {
            ESP_LOGW(SESSION_MANAGER_TAG, "Failed to save authoritative session info for %s", TeslaBLE::domain_to_string(domain));
            return -1;
        }
        
        ESP_LOGI(SESSION_MANAGER_TAG, "Forced session update for %s with vehicle's authoritative counter %u", 
                 TeslaBLE::domain_to_string(domain), session_info.counter);
        return 0;
    } else {
        // Other errors
        ESP_LOGE(SESSION_MANAGER_TAG, "Failed to update session for %s: %d", TeslaBLE::domain_to_string(domain), result);
        return result;
    }
}

void SessionManager::invalidate_session(UniversalMessage_Domain domain) {
    ESP_LOGI(SESSION_MANAGER_TAG, "Invalidating session for %s", TeslaBLE::domain_to_string(domain));
    
    auto peer = tesla_client_->getPeer(domain);
    if (peer) {
        peer->setIsValid(false);
    }
    
    // Optionally remove from NVS
    const char* nvs_key = get_nvs_key_for_domain(domain);
    if (nvs_key) {
        nvs_erase_key(storage_handle_, nvs_key);
        nvs_commit(storage_handle_);
    }
}

bool SessionManager::is_domain_authenticated(UniversalMessage_Domain domain) {
    auto peer = tesla_client_->getPeer(domain);
    return peer ? peer->isInitialized() : false;
}

bool SessionManager::request_session_info(UniversalMessage_Domain domain) {
    ESP_LOGD(SESSION_MANAGER_TAG, "Requesting session info for %s", TeslaBLE::domain_to_string(domain));
    
    if (!tesla_client_) {
        ESP_LOGE(SESSION_MANAGER_TAG, "Tesla client not available");
        return false;
    }
    
    unsigned char message_buffer[MAX_BLE_MESSAGE_SIZE];
    size_t message_length = 0;
    
    int result = tesla_client_->buildSessionInfoRequestMessage(domain, message_buffer, &message_length);
    if (result != 0) {
        ESP_LOGE(SESSION_MANAGER_TAG, "Failed to build session info request: %d", result);
        return false;
    }
    
    // Send via BLE manager
    auto* ble_manager = parent_->get_ble_manager();
    if (!ble_manager) {
        ESP_LOGE(SESSION_MANAGER_TAG, "BLE manager not available");
        return false;
    }
    
    return ble_manager->write_message(message_buffer, message_length) == 0;
}

bool SessionManager::start_pairing(const std::string& role) {
    ESP_LOGI(SESSION_MANAGER_TAG, "Starting pairing with role: %s", role.c_str());
    
    if (!tesla_client_) {
        ESP_LOGE(SESSION_MANAGER_TAG, "Tesla client not available");
        return false;
    }
    
    // Convert role string to enum
    Keys_Role role_enum = Keys_Role_ROLE_DRIVER;
    if (role == "ROLE_CHARGING_MANAGER") {
        role_enum = Keys_Role_ROLE_CHARGING_MANAGER;
    } else if (role == "ROLE_DRIVER") {
        role_enum = Keys_Role_ROLE_DRIVER;
    }
    
    unsigned char whitelist_message_buffer[MAX_BLE_MESSAGE_SIZE];
    size_t whitelist_message_length = 0;
    
    int result = tesla_client_->buildWhiteListMessage(
        role_enum, 
        VCSEC_KeyFormFactor_KEY_FORM_FACTOR_CLOUD_KEY,
        whitelist_message_buffer, 
        &whitelist_message_length);
    
    if (result != 0) {
        ESP_LOGE(SESSION_MANAGER_TAG, "Failed to build whitelist message: %d", result);
        return false;
    }
    
    // Send via BLE manager
    auto* ble_manager = parent_->get_ble_manager();
    if (!ble_manager) {
        ESP_LOGE(SESSION_MANAGER_TAG, "BLE manager not available");
        return false;
    }
    
    if (ble_manager->write_message(whitelist_message_buffer, whitelist_message_length) == 0) {
        ESP_LOGI(SESSION_MANAGER_TAG, "Pairing request sent. Please tap your card on the reader now.");
        return true;
    }
    
    return false;
}

// Private helper methods
bool SessionManager::load_from_nvs(const char* key, std::vector<uint8_t>& data) {
    size_t required_size = 0;
    esp_err_t err = nvs_get_blob(storage_handle_, key, nullptr, &required_size);
    if (err != ESP_OK) {
        return false;
    }
    
    data.resize(required_size);
    err = nvs_get_blob(storage_handle_, key, data.data(), &required_size);
    return err == ESP_OK;
}

bool SessionManager::save_to_nvs(const char* key, const void* data, size_t size) {
    esp_err_t err = nvs_set_blob(storage_handle_, key, data, size);
    if (err != ESP_OK) {
        ESP_LOGE(SESSION_MANAGER_TAG, "Failed to set NVS key %s: %s", key, esp_err_to_name(err));
        return false;
    }
    
    err = nvs_commit(storage_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(SESSION_MANAGER_TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
        return false;
    }
    
    return true;
}

const char* SessionManager::get_nvs_key_for_domain(UniversalMessage_Domain domain) {
    switch (domain) {
        case UniversalMessage_Domain_DOMAIN_INFOTAINMENT:
            return NVS_KEY_INFOTAINMENT;
        case UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY:
            return NVS_KEY_VCSEC;
        default:
            return nullptr;
    }
}

void SessionManager::log_session_info(const Signatures_SessionInfo& session_info) {
    ESP_LOGD(SESSION_MANAGER_TAG, "Session Info:");
    ESP_LOGD(SESSION_MANAGER_TAG, "  Status: %d", session_info.status);
    ESP_LOGD(SESSION_MANAGER_TAG, "  Counter: %u", session_info.counter);
    ESP_LOGD(SESSION_MANAGER_TAG, "  Clock time: %u", session_info.clock_time);
}

bool SessionManager::encode_session_info(const Signatures_SessionInfo& session_info, std::vector<uint8_t>& encoded) {
    size_t buffer_size = Signatures_SessionInfo_size + 10; // Add padding
    encoded.resize(buffer_size);
    
    pb_ostream_t stream = pb_ostream_from_buffer(encoded.data(), buffer_size);
    
    if (!pb_encode(&stream, Signatures_SessionInfo_fields, &session_info)) {
        ESP_LOGE(SESSION_MANAGER_TAG, "Failed to encode session info: %s", PB_GET_ERROR(&stream));
        return false;
    }
    
    encoded.resize(stream.bytes_written);
    return true;
}

bool SessionManager::decode_session_info(const std::vector<uint8_t>& encoded, Signatures_SessionInfo& session_info) {
    pb_istream_t stream = pb_istream_from_buffer(encoded.data(), encoded.size());
    
    if (!pb_decode(&stream, Signatures_SessionInfo_fields, &session_info)) {
        ESP_LOGE(SESSION_MANAGER_TAG, "Failed to decode session info: %s", PB_GET_ERROR(&stream));
        return false;
    }
    
    return true;
}

} // namespace tesla_ble_vehicle
} // namespace esphome
