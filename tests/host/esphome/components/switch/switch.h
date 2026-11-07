#pragma once
#include <entity.h>
namespace esphome::switch_ {
struct Switch : host::Entity<bool> { virtual void write_state(bool) = 0; };
}
