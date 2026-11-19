#include "dht11.h"

namespace olvar {

namespace {

// The digits of n placed after the decimal point: 1 -> 0.1, 12 -> 0.12, 0 -> 0.
float fraction(uint8_t n) {
  if (n < 10) return n / 10.0f;
  if (n < 100) return n / 100.0f;
  return n / 1000.0f;
}

}  // namespace

void dht11PulsesToBytes(const uint16_t highUs[kDht11Bits], uint8_t bytes[5]) {
  for (size_t i = 0; i < 5; ++i) bytes[i] = 0;
  for (size_t i = 0; i < kDht11Bits; ++i) {
    bytes[i / 8] = static_cast<uint8_t>(bytes[i / 8] << 1);
    if (highUs[i] >= kDht11OneMinUs) bytes[i / 8] |= 1;
  }
}

TempHumidity dht11Decode(const uint8_t bytes[5]) {
  TempHumidity r;
  uint8_t sum = static_cast<uint8_t>(bytes[0] + bytes[1] + bytes[2] + bytes[3]);
  if (sum != bytes[4]) return r;
  if ((bytes[0] | bytes[1] | bytes[2] | bytes[3]) == 0) return r;  // line held low, not a reading
  float h = bytes[0] + fraction(bytes[1]);
  float t = bytes[2] + fraction(bytes[3] & 0x7F);
  if (bytes[3] & 0x80) t = -t;
  if (h > 100 || t < -40 || t > 80) return r;  // garbage filter, wider than the -20..60 C rating
  r.temperature_c = {true, t};
  r.humidity_pct = {true, h};
  return r;
}

}  // namespace olvar
