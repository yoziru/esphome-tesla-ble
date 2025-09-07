#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <any>
#include <vector>
#include <esphome/core/helpers.h>
#include <universal_message.pb.h>

namespace esphome {
namespace tesla_ble_vehicle {

// Forward declarations
class TeslaBLEVehicle;
class SessionManager;
class BLEManager;
class CommandManager;

namespace TeslaBLE {
    class Client;
}

/**
 * @brief Result type for command operations
 */
enum class CommandResult {
    SUCCESS = 0,
    ERROR_NO_SESSION_MANAGER = -1,
    ERROR_NO_BLE_MANAGER = -2,
    ERROR_NO_CLIENT = -3,
    ERROR_NO_PEER = -4,
    ERROR_BUILD_MESSAGE = -5,
    ERROR_SEND_MESSAGE = -6,
    ERROR_INVALID_DOMAIN = -7,
    ERROR_AUTH_REQUIRED = -8,
    ERROR_NO_COMMAND_MANAGER = -9,
    ERROR_NO_VEHICLE = -10
};

/**
 * @brief Domain-specific command strategy interface
 */
class DomainCommandStrategy {
public:
    virtual ~DomainCommandStrategy() = default;

    /**
     * @brief Check if domain requires authentication
     */
    virtual bool requires_authentication() const = 0;

    /**
     * @brief Get the domain this strategy handles
     */
    virtual UniversalMessage_Domain get_domain() const = 0;

    /**
     * @brief Check if domain is currently authenticated
     */
    virtual bool is_authenticated(SessionManager* session_manager) const = 0;

    /**
     * @brief Handle counter increment for this domain
     */
    virtual CommandResult increment_counter(SessionManager* session_manager) const = 0;

    /**
     * @brief Get domain-specific name for logging
     */
    virtual const char* get_name() const = 0;
};

/**
 * @brief VCSEC domain command strategy
 */
class VCSECDomainStrategy : public DomainCommandStrategy {
public:
    UniversalMessage_Domain get_domain() const override {
        return UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY;
    }

    bool requires_authentication() const override { return true; }

    bool is_authenticated(SessionManager* session_manager) const override;

    CommandResult increment_counter(SessionManager* session_manager) const override;

    const char* get_name() const override { return "VCSEC"; }
};

/**
 * @brief Infotainment domain command strategy
 */
class InfotainmentDomainStrategy : public DomainCommandStrategy {
public:
    UniversalMessage_Domain get_domain() const override {
        return UniversalMessage_Domain_DOMAIN_INFOTAINMENT;
    }

    bool requires_authentication() const override { return true; }

    bool is_authenticated(SessionManager* session_manager) const override;

    CommandResult increment_counter(SessionManager* session_manager) const override;

    const char* get_name() const override { return "Infotainment"; }
};

/**
 * @brief Broadcast domain command strategy (no auth required)
 */
class BroadcastDomainStrategy : public DomainCommandStrategy {
public:
    UniversalMessage_Domain get_domain() const override {
        return UniversalMessage_Domain_DOMAIN_BROADCAST;
    }

    bool requires_authentication() const override { return false; }

    bool is_authenticated(SessionManager* session_manager) const override { return true; }

    CommandResult increment_counter(SessionManager* session_manager) const override { return CommandResult::SUCCESS; }

    const char* get_name() const override { return "Broadcast"; }
};

/**
 * @brief Fluent API for building BLE commands with proper domain handling
 */
class BLECommandBuilder {
public:
    /**
     * @brief Create a command builder for a specific domain
     */
    static BLECommandBuilder for_domain(UniversalMessage_Domain domain);

    /**
     * @brief Create a command builder with a custom strategy
     */
    static BLECommandBuilder with_strategy(std::shared_ptr<DomainCommandStrategy> strategy);

    /**
     * @brief Set the vehicle instance (provides access to managers)
     */
    BLECommandBuilder& with_vehicle(TeslaBLEVehicle* vehicle);

    /**
     * @brief Set explicit managers (alternative to vehicle)
     */
    BLECommandBuilder& with_managers(SessionManager* session_manager, BLEManager* ble_manager);

    /**
     * @brief Set the message builder function
     */
    template<typename BuilderFunc>
    BLECommandBuilder& with_builder(BuilderFunc builder) {
        builder_func_ = builder;
        return *this;
    }

    /**
     * @brief Set command name for logging
     */
    BLECommandBuilder& with_name(const std::string& name);

    /**
     * @brief Force counter increment (override strategy default)
     */
    BLECommandBuilder& force_counter_increment(bool increment = true);

    /**
     * @brief Build the command function
     */
    std::function<int()> build();

    /**
     * @brief Build and enqueue the command
     */
    CommandResult enqueue(CommandManager* command_manager);

private:
    BLECommandBuilder(std::shared_ptr<DomainCommandStrategy> strategy);

    std::shared_ptr<DomainCommandStrategy> strategy_;
    TeslaBLEVehicle* vehicle_;
    SessionManager* session_manager_;
    BLEManager* ble_manager_;
    std::function<int(TeslaBLE::Client*, unsigned char*, size_t*)> builder_func_;
    std::string command_name_;
    bool force_counter_increment_;
};

/**
 * @brief Registry for domain strategies
 */
class DomainStrategyRegistry {
public:
    static DomainStrategyRegistry& get_instance();

    void register_strategy(UniversalMessage_Domain domain, std::shared_ptr<DomainCommandStrategy> strategy);
    std::shared_ptr<DomainCommandStrategy> get_strategy(UniversalMessage_Domain domain) const;

private:
    DomainStrategyRegistry();
    std::unordered_map<UniversalMessage_Domain, std::shared_ptr<DomainCommandStrategy>> strategies_;
};

/**
 * @brief Command type enumeration for common Tesla operations
 * 
 * To add a new command:
 * 1. Add the enum value here
 * 2. Register it in TeslaCommandRegistry::TeslaCommandRegistry() constructor
 * 3. That's it! Everything else is handled automatically.
 */
enum class TeslaCommandType {
    // VCSEC Commands
    VCSEC_STATUS_POLL,
    WAKE_VEHICLE,
    
    // Infotainment Commands
    INFOTAINMENT_DATA_POLL,
    SET_CHARGING_AMPS,
    SET_CHARGING_LIMIT,
    SET_CHARGING_STATE,
    
    // Custom command (for future extensibility)
    CUSTOM
};

/**
 * @brief Command definition structure - encapsulates everything about a command
 */
struct TeslaCommandDefinition {
    TeslaCommandType type;
    UniversalMessage_Domain domain;
    std::string name;
    std::function<int(TeslaBLE::Client*, unsigned char*, size_t*, const std::vector<std::any>&)> builder_func;
};

/**
 * @brief Command registry - single source of truth for all commands
 */
class TeslaCommandRegistry {
public:
    static TeslaCommandRegistry& get_instance();
    
    const TeslaCommandDefinition* get_definition(TeslaCommandType type) const;
    std::vector<TeslaCommandType> get_all_types() const;

private:
    TeslaCommandRegistry();
    
    std::unordered_map<TeslaCommandType, TeslaCommandDefinition> definitions_;
    
    void register_command(TeslaCommandType type, UniversalMessage_Domain domain, 
                         const std::string& name,
                         std::function<int(TeslaBLE::Client*, unsigned char*, size_t*, const std::vector<std::any>&)> builder);
};

/**
 * @brief High-level command factory for common operations
 */
class TeslaCommandFactory {
public:
    explicit TeslaCommandFactory(TeslaBLEVehicle* vehicle);

    /**
     * @brief Create a command using a predefined type and parameters
     */
    BLECommandBuilder create(TeslaCommandType type, const std::vector<std::any>& params = {});
    
    /**
     * @brief Create a custom command with full control
     */
    BLECommandBuilder create_custom(UniversalMessage_Domain domain, 
                                   const std::string& name,
                                   std::function<int(TeslaBLE::Client*, unsigned char*, size_t*)> builder);

private:
    TeslaBLEVehicle* vehicle_;
};

} // namespace tesla_ble_vehicle
} // namespace esphome
