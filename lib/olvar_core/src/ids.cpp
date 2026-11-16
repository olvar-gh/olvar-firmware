#include "ids.h"

#include <stdio.h>
#include <string.h>

namespace {

constexpr size_t kMinIdLen = 3;
constexpr size_t kMaxIdLen = 32;
constexpr const char* kReservedId = "olvar-gateway";
constexpr const char* kHex = "0123456789abcdef";

bool isLowerAlnum(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }

size_t topic(const char* id, const char* leaf, char* out, size_t cap) {
  if (id == nullptr || out == nullptr || cap == 0) return 0;
  int n = snprintf(out, cap, "olvar/v1/%s/%s", id, leaf);
  if (n < 0 || static_cast<size_t>(n) >= cap) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

}  // namespace

bool isValidDeviceId(const char* id) {
  if (id == nullptr) return false;
  size_t len = strnlen(id, kMaxIdLen + 1);
  if (len < kMinIdLen || len > kMaxIdLen) return false;
  if (!isLowerAlnum(id[0])) return false;
  for (size_t i = 1; i < len; ++i) {
    if (!isLowerAlnum(id[i]) && id[i] != '-') return false;
  }
  return strcmp(id, kReservedId) != 0;
}

void deviceIdFromMac(const uint8_t mac[6], char out[13]) {
  memcpy(out, "olvar-", 6);
  for (int i = 0; i < 3; ++i) {
    out[6 + 2 * i] = kHex[mac[3 + i] >> 4];
    out[7 + 2 * i] = kHex[mac[3 + i] & 0x0f];
  }
  out[12] = '\0';
}

void bootIdFromRandom(uint32_t r, char out[9]) {
  for (int i = 7; i >= 0; --i) {
    out[i] = kHex[r & 0x0f];
    r >>= 4;
  }
  out[8] = '\0';
}

size_t telemetryTopic(const char* id, char* out, size_t cap) { return topic(id, "telemetry", out, cap); }

size_t statusTopic(const char* id, char* out, size_t cap) { return topic(id, "status", out, cap); }
