#pragma once
#include <entity.h>
namespace esphome::climate {
enum ClimateMode { CLIMATE_MODE_OFF, CLIMATE_MODE_HEAT_COOL };
constexpr int CLIMATE_SUPPORTS_CURRENT_TEMPERATURE = 1;
struct ClimateTraits {
  void set_supported_modes(std::initializer_list<ClimateMode>) {}
  void add_feature_flags(int) {}
  void set_visual_min_temperature(float) {}
  void set_visual_max_temperature(float) {}
  void set_visual_temperature_step(float) {}
};
struct ClimateCall {
  std::optional<ClimateMode> get_mode() const { return {}; }
  std::string get_custom_preset() const { return {}; }
  std::string get_custom_fan_mode() const { return {}; }
  std::optional<float> get_target_temperature() const { return {}; }
};
struct Climate {
  ClimateMode mode = CLIMATE_MODE_OFF;
  float current_temperature = NAN, target_temperature = NAN;
  virtual ClimateTraits traits() = 0;
  virtual void control(const ClimateCall &) = 0;
  void set_supported_custom_presets(std::initializer_list<std::string>) {}
  void set_supported_custom_fan_modes(std::initializer_list<std::string>) {}
  void set_custom_preset_(const std::string &) {}
  void set_custom_fan_mode_(const std::string &) {}
  void publish_state() {}
};
}
