#pragma once
namespace esphome {
struct HostPreference {
  template<typename T> bool load(T *) { return false; }
  template<typename T> bool save(const T *) { return true; }
};
struct HostPreferences {
  template<typename T> HostPreference make_preference(unsigned) { return {}; }
};
inline HostPreferences preferences;
inline auto *global_preferences = &preferences;
}
