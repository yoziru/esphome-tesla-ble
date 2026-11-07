#pragma once
#include <cstdarg>
#define ESP_LOGCONFIG(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGV(...) ((void)0)
enum { ESPHOME_LOG_LEVEL_ERROR, ESPHOME_LOG_LEVEL_WARN, ESPHOME_LOG_LEVEL_INFO,
       ESPHOME_LOG_LEVEL_DEBUG, ESPHOME_LOG_LEVEL_VERBOSE };
inline void esp_log_vprintf_(int, const char *, int, const char *, va_list) {}
