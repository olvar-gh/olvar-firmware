// DHT11 frame decoding and read pacing (ASAIR DHT11, single-wire bus).
// Pure C++17, no Arduino.h, so it builds in native tests; the pin timing is in src/sensors/.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "reading.h"

namespace olvar {

constexpr size_t kDht11Bits = 40;
// A data bit's high pulse is 26-28 us for 0 and ~70 us for 1.
constexpr uint16_t kDht11OneMinUs = 50;

// Packs 40 high-pulse widths (us, MSB first) into 5 bytes.
void dht11PulsesToBytes(const uint16_t highUs[kDht11Bits], uint8_t bytes[5]);

// bytes: humidity int, humidity tenths, temperature int, temperature tenths (bit 7 = below 0 C),
// checksum = low byte of the sum of the first four. A bad checksum, an all-zero frame or a
// value outside what the part can report gives ok=false for both.
TempHumidity dht11Decode(const uint8_t bytes[5]);

// The DHT11 must not be read more than once per 2 s. A call inside that window gets the
// previous attempt's result (null if it failed), never an older good value.
class Dht11Throttle {
 public:
  static constexpr uint32_t kMinIntervalMs = 2000;

  bool due(uint32_t nowMs) const { return !started_ || nowMs - lastMs_ >= kMinIntervalMs; }
  void mark(uint32_t nowMs) {
    started_ = true;
    lastMs_ = nowMs;
  }

 private:
  bool started_ = false;
  uint32_t lastMs_ = 0;
};

}  // namespace olvar
