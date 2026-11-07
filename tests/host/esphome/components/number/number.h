#pragma once
#include <entity.h>
namespace esphome::number {
struct NumberTraits {
  float min = 0, max = 100;
  float get_min_value() const { return min; }
  float get_max_value() const { return max; }
  void set_max_value(float value) { max = value; }
};
struct Number : host::Entity<float> {
  NumberTraits traits;
  virtual void control(float) = 0;
};
}
