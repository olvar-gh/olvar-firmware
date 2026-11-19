#include "dht11.h"

namespace olvar {

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
  float h = bytes[0] + (bytes[1] % 10) / 10.0f;
  float t = bytes[2] + (bytes[3] & 0x0F) / 10.0f;
  if (bytes[3] & 0x80) t = -t;
  if (h > 100 || t < -20 || t > 60) return r;
  r.temperature_c = {true, t};
  r.humidity_pct = {true, h};
  return r;
}

}  // namespace olvar
