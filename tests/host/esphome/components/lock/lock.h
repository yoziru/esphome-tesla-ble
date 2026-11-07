#pragma once
#include <entity.h>
namespace esphome::lock {
enum LockState { LOCK_STATE_NONE, LOCK_STATE_LOCKED, LOCK_STATE_UNLOCKED, LOCK_STATE_LOCKING, LOCK_STATE_UNLOCKING };
struct LockCall { std::optional<LockState> get_state() const { return {}; } };
struct Lock : host::Entity<LockState> { virtual void control(const LockCall &) = 0; };
}
