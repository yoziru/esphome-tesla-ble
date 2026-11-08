// SPDX-License-Identifier: AGPL-3.0-only
// Producer-owned, reboot-based charging-state reconciliation. No ESPHome or HA timestamp trust.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <string>

namespace esphome { namespace tesla_ble_vehicle {

struct StopRecoveryStore {
  uint32_t magic{0x50535232};
  uint32_t generation{0};
  char nonce[33]{};
  char previous_boot[17]{};
  char vin[18]{};
  bool locked{false};
};

inline bool recovery_hex(const std::string &s, size_t length) {
  if (s.size() != length) return false;
  for (char c : s) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  return true;
}

inline bool recovery_payload(const uint8_t *frame, size_t length,
                             const uint8_t *&payload, size_t &payload_length) {
  // TeslaBLE::Client::buildCarServerActionPayload prepends the two-byte
  // big-endian BLE frame length. It is not part of the protobuf message.
  if (length < 2) return false;
  payload_length = (size_t(frame[0]) << 8) | frame[1];
  if (payload_length == 0 || payload_length != length - 2) return false;
  payload = frame + 2;
  return true;
}

class StopRecoveryProtocol {
 public:
  StopRecoveryStore saved{};
  std::string boot;
  // i=idle, p=reboot pending, h=held, q=read only, v=verified, r=released.
  char phase{'i'};
  uint32_t sequence{0}, sample_ms{0}, last_counted_ms{0};
  uint8_t zero_samples{0};
  char stopped_state{'?'};
  std::string expected_uuid;

  bool locked() const { return saved.locked; }
  bool reading() const { return locked() && (phase == 'q' || phase == 'v'); }

  void load(const StopRecoveryStore &record, const std::string &new_boot) {
    boot = new_boot;
    saved = record;
    // Bound every persisted character array before constructing a string.
    saved.nonce[32] = 0;
    saved.previous_boot[16] = 0;
    saved.vin[17] = 0;
    if (saved.magic != 0x50535232 || !recovery_hex(boot, 16)) {
      saved = StopRecoveryStore{};
      saved.locked = true;
      phase = 'h';
    } else if (saved.locked) {
      phase = recovery_hex(saved.nonce, 32) && recovery_hex(saved.previous_boot, 16)
          && saved.previous_boot != boot && saved.generation > 0 ? 'q' : 'h';
    }
  }

  bool prepare(const std::string &nonce) {
    if (!recovery_hex(nonce, 32) || !recovery_hex(boot, 16)
        || nonce == saved.nonce || phase == 'p'
        || saved.generation == std::numeric_limits<uint32_t>::max()) return false;
    // Persist the lock before scheduling the reboot. Until then the loop and
    // all mutation admissions are stopped; no isolation proof is claimed yet.
    saved.locked = true;
    ++saved.generation;
    nonce.copy(saved.nonce, 32); saved.nonce[32] = 0;
    boot.copy(saved.previous_boot, 16); saved.previous_boot[16] = 0;
    phase = 'p';
    zero_samples = 0; sequence = 0; expected_uuid.clear();
    return true;
  }

  bool expect(const std::string &uuid) {
    if (!reading() || !recovery_hex(uuid, 32)) return false;
    expected_uuid = uuid;
    return true;
  }

  bool sample(const std::string &uuid, uint32_t now_ms, bool complete,
              char charge_state, float actual_amps, float power_kw) {
    if (!reading() || expected_uuid.empty() || uuid != expected_uuid) return false;
    expected_uuid.clear(); // a duplicate response cannot become another sample
    if (sequence == std::numeric_limits<uint32_t>::max()) {
      phase = 'h'; return false;
    }
    ++sequence;
    sample_ms = now_ms;
    const bool zero = complete && std::isfinite(actual_amps) && actual_amps == 0
        && std::isfinite(power_kw) && power_kw == 0
        && (charge_state == 'S' || charge_state == 'C' || charge_state == 'D' || charge_state == 'N');
    if (!zero) {
      zero_samples = 0; phase = 'q'; stopped_state = '?';
    } else if (zero_samples == 0 || uint32_t(now_ms - last_counted_ms) >= 1000) {
      if (zero_samples < 2) ++zero_samples;
      last_counted_ms = now_ms;
      stopped_state = charge_state;
      phase = zero_samples >= 2 ? 'v' : 'q';
    }
    return true;
  }

  bool can_release(const std::string &nonce, uint32_t now_ms) const {
    return reading() && phase == 'v' && nonce == saved.nonce && zero_samples == 2
        && uint32_t(now_ms - sample_ms) <= 5000;
  }

  void released() { saved.locked = false; phase = 'r'; }
  void held() { saved.locked = true; phase = 'h'; expected_uuid.clear(); }
};

}}  // namespace esphome::tesla_ble_vehicle
