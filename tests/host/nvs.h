#pragma once
#include <cstddef>
#include <cstring>
#include <vector>
using nvs_handle_t = unsigned;
constexpr int ESP_OK = 0, ESP_ERR_NVS_NOT_FOUND = 1, NVS_READWRITE = 2;
inline std::vector<unsigned char> host_nvs_record, host_nvs_pending;
inline bool host_nvs_fail = false;
inline int nvs_open(const char *, int, nvs_handle_t *handle) { *handle = 1; return host_nvs_fail ? -1 : ESP_OK; }
inline void nvs_close(nvs_handle_t) {}
inline int nvs_get_blob(nvs_handle_t, const char *, void *value, size_t *size) {
  if (host_nvs_record.empty()) return ESP_ERR_NVS_NOT_FOUND;
  if (*size != host_nvs_record.size()) return -1;
  std::memcpy(value, host_nvs_record.data(), *size); return ESP_OK;
}
inline int nvs_set_blob(nvs_handle_t, const char *, const void *value, size_t size) {
  if (host_nvs_fail) return -1;
  auto bytes = static_cast<const unsigned char *>(value);
  host_nvs_pending.assign(bytes, bytes+size); return ESP_OK;
}
inline int nvs_commit(nvs_handle_t) {
  if (host_nvs_fail) return -1;
  host_nvs_record = host_nvs_pending; return ESP_OK;
}
