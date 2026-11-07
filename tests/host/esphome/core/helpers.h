#pragma once
#include <cstdint>
#include <string>
#include "component.h"
namespace esphome {
inline uint32_t fnv1_hash_extend(uint32_t hash, const std::string &s) {
  for (unsigned char c : s) hash = (hash * 16777619U) ^ c;
  return hash;
}
inline uint32_t fnv1_hash(const std::string &s) { return fnv1_hash_extend(2166136261U, s); }
}
