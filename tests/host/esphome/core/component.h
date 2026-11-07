#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace esphome {
inline uint32_t host_millis = 100;
inline uint32_t millis() { return host_millis; }
class PollingComponent {
 public:
  virtual ~PollingComponent() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void update() {}
  virtual void dump_config() {}
  bool status_has_warning() const { return warning_; }
  void status_set_warning(const char *) { warning_ = true; }
  void status_clear_warning() { warning_ = false; }
  void status_momentary_warning(const std::string &name, uint32_t length = 5000) {
    status_set_warning(name.c_str());
    timeouts_[name] = millis() + length;
  }
  bool cancel_timeout(const std::string &name) { return timeouts_.erase(name) != 0; }
  void run_timeouts() {
    for (auto it = timeouts_.begin(); it != timeouts_.end();) {
      if (static_cast<int32_t>(millis() - it->second) >= 0) {
        status_clear_warning();
        it = timeouts_.erase(it);
      } else ++it;
    }
  }
 private:
  bool warning_ = false;
  std::map<std::string, uint32_t> timeouts_;
};
}
