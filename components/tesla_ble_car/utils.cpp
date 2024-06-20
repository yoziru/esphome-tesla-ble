#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace TeslaBLE {
uint8_t *HexStrToUint8(const char *string) {
  if (string == NULL)
	return NULL;

  size_t slength = strlen(string);
  if ((slength % 2) != 0) // must be even
	return NULL;

  size_t dlength = slength / 2;
  uint8_t *data = (uint8_t *)malloc(dlength);
  memset(data, 0, dlength);
  size_t index = 0;

  while (index < slength) {
	char c = string[index];
	int value = 0;
	if (c >= '0' && c <= '9')
	  value = (c - '0');
	else if (c >= 'A' && c <= 'F')
	  value = (10 + (c - 'A'));
	else if (c >= 'a' && c <= 'f')
	  value = (10 + (c - 'a'));
	else
	  return NULL;

	data[(index / 2)] += value << (((index + 1) % 2) * 4);
	index++;
  }

  return data;
}

std::string Uint8ToHexString(const uint8_t *v, size_t s) {
  std::stringstream stream;
  stream << std::hex << std::setfill('0');
  for (int i = 0; i < s; i++) {
	stream << std::hex << std::setw(2) << static_cast<int>(v[i]);
  }
  return stream.str();
}

void DumpHexBuffer(const char *title, unsigned char *buf, size_t len) {
  size_t i = 0;
  printf("\n%s", title);
  for (i = 0; i < len; i++) {
	printf("%c%c", "0123456789ABCDEF"[buf[i] / 16],
		   "0123456789ABCDEF"[buf[i] % 16]);
  }
  printf("\n");
}

void DumpBuffer(const char *title, unsigned char *buf, size_t len) {
  size_t i = 0;
  printf("\n%s", title);
  for (i = 0; i < len; i++) {
	printf("%c", buf[i]);
  }
  printf("\n");
}
}
