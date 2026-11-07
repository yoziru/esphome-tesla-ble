#include "tesla_ble_vehicle.h"

// Host boundaries only: no BLE radio or persistent NVS. Vehicle and state
// manager behavior come from the real library/component translation units.
namespace esphome::tesla_ble_vehicle {
BleAdapterImpl::BleAdapterImpl(TeslaBLEVehicle *parent) : parent_(parent) {}
void BleAdapterImpl::connect(const std::string &) {}
void BleAdapterImpl::disconnect() {}
bool BleAdapterImpl::write(const std::vector<uint8_t> &) { return true; }
void BleAdapterImpl::process_write_queue() {}
void BleAdapterImpl::clear_queues() {}

StorageAdapterImpl::StorageAdapterImpl() : storage_handle_(0), initialized_(false) {}
StorageAdapterImpl::~StorageAdapterImpl() = default;
bool StorageAdapterImpl::initialize() { initialized_ = true; return true; }
bool StorageAdapterImpl::load(const std::string &, std::vector<uint8_t> &) { return false; }
bool StorageAdapterImpl::save(const std::string &, const std::vector<uint8_t> &) { return true; }
bool StorageAdapterImpl::remove(const std::string &) { return true; }
}
