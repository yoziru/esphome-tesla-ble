#pragma once
namespace esphome::esp32_ble_tracker {
enum class ClientState { IDLE, CONNECTED, ESTABLISHED, DISCONNECTING };
struct ESPBTUUID { static ESPBTUUID from_raw(const char *) { return {}; } };
}
