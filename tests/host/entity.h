#pragma once
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace esphome::host {
template<typename T> struct Entity {
  T state{};
  std::vector<T> published;
  bool has_state() const { return has_state_; }
  void set_has_state(bool value) { has_state_ = value; }
  void publish_state(T value) { state = value; has_state_ = true; published.push_back(value); }
 private:
  bool has_state_ = false;
};
}
