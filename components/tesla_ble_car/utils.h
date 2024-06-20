#ifndef TESLA_BLE_INCLUDE_UTILS_H_
#define TESLA_BLE_INCLUDE_UTILS_H_

#include <cstdint>
#include <string>

namespace TeslaBLE {
std::string Uint8ToHexString(const uint8_t *v, size_t s);

uint8_t *HexStrToUint8(const char *string);

void DumpBuffer(const char *title, unsigned char *buf, size_t len);

void DumpHexBuffer(const char *title, unsigned char *buf, size_t len);
}
#endif // TESLA_BLE_INCLUDE_UTILS_H_