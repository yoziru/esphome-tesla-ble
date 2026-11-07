#pragma once
#include <entity.h>
namespace esphome::select {
struct Select {
  virtual void control(size_t) = 0;
  std::optional<size_t> active_index() const { return index; }
  bool has_index(size_t value) const { return value < options.size(); }
  void publish_state(size_t value) { index = value; }
  std::vector<std::string> options;
  std::optional<size_t> index;
};
}
