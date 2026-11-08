#pragma once
namespace esphome {
struct HostApplication { unsigned reboots{0}; void safe_reboot() { ++reboots; } };
inline HostApplication App;
}
