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

// bytes: humidity int, humidity fraction, temperature int, temperature fraction (bit 7 = below
// 0 C). A fraction byte holds the digits after the point: 0x01 is .1, 0x0C is .12. Checksum = low byte of the sum of the first four. A bad checksum, an all-zero frame or a
// value outside what the part can report gives ok=false for both.
TempHumidity dht11Decode(const uint8_t bytes[5]);

// The DHT11 needs 2 s between the end of one transaction and the next start signal. A call
// inside that window gets the previous attempt's result (null if it failed), never an older good
// value.
class Dht11Throttle {
 public:
  static constexpr uint32_t kMinIntervalMs = 2000;

  bool due(uint32_t nowMs) const { return !started_ || nowMs - lastMs_ >= kMinIntervalMs; }
  // Absolute millis() at which the next transaction may start (nowMs if it already may).
  uint32_t dueAtMs(uint32_t nowMs) const { return due(nowMs) ? nowMs : lastMs_ + kMinIntervalMs; }
  // Call when a transaction has ended.
  void mark(uint32_t nowMs) {
    started_ = true;
    lastMs_ = nowMs;
  }

 private:
  bool started_ = false;
  uint32_t lastMs_ = 0;
};

}  // namespace olvar
