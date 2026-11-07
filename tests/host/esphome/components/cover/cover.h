#pragma once
#include <entity.h>
namespace esphome::cover {
constexpr float COVER_OPEN = 1, COVER_CLOSED = 0;
struct CoverTraits {
  void set_supports_position(bool) {}
  void set_supports_tilt(bool) {}
  void set_supports_stop(bool) {}
  void set_is_assumed_state(bool) {}
};
struct CoverCall { std::optional<float> get_position() const { return {}; } };
struct Cover {
  float position = 0;
  virtual CoverTraits get_traits() = 0;
  virtual void control(const CoverCall &) = 0;
  void publish_state() {}
};
}
