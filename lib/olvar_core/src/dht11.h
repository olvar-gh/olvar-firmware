// DHT11 frame decoding and read pacing (ASAIR DHT11, single-wire bus).
// Pure C++17, no Arduino.h, so it builds in native tests; the pin timing is in src/sensors/.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "reading.h"

namespace olvar {

constexpr size_t kDht11Bits = 40;
// A data bit's high pulse is 23-27 us for 0 and 68-74 us for 1 (ASAIR datasheet 2022-12 §5.4).
constexpr uint16_t kDht11OneMinUs = 50;
// Longest phase the sensor drives: 87 us response high (88 max).
constexpr uint32_t kDht11PhaseTimeoutUs = 100;

// Packs 40 high-pulse widths (us, MSB first) into 5 bytes.
void dht11PulsesToBytes(const uint16_t highUs[kDht11Bits], uint8_t bytes[5]);

// How far one reply got. Release: the line never rose after the start signal (no pull-up, DATA
// shorted to GND). NoResponse: it rose but the sensor never pulled it low (no power, wrong pin).
// Response: the 83 us low + 87 us high reply was cut short. Data: a data bit timed out.
enum class Dht11Stage : uint8_t { Release, NoResponse, Response, Data, Done };

struct Dht11Pulses {
  Dht11Stage stage = Dht11Stage::Release;
  uint8_t bits = 0;  // data bits timed, highUs[0..bits)
  uint16_t highUs[kDht11Bits] = {};
};

namespace detail {

// Waits while the line reads `level`; false after timeoutUs. us = how long it stayed there.
template <typename Line>
bool dht11WaitWhile(Line& line, int level, uint16_t& us) {
  uint32_t t0 = line.us();
  uint32_t dt = 0;
  while (line.level() == level) {
    dt = line.us() - t0;
    if (dt > kDht11PhaseTimeoutUs) return false;
  }
  us = static_cast<uint16_t>(dt);
  return true;
}

}  // namespace detail

// Times the sensor's reply right after the host has released the line at the end of the start
// signal. Line has int level() (0/1) and uint32_t us() (a microsecond clock).
template <typename Line>
void dht11WalkReply(Line& line, Dht11Pulses& p) {
  constexpr int kLow = 0, kHigh = 1;
  uint16_t us = 0;
  p.bits = 0;
  // The pull-up needs a moment to raise the line. Reading it before then would take the
  // release for the sensor's response and shift every bit by one.
  p.stage = Dht11Stage::Release;
  if (!detail::dht11WaitWhile(line, kLow, us)) return;
  // Released high (10-20 us) until the sensor answers.
  p.stage = Dht11Stage::NoResponse;
  if (!detail::dht11WaitWhile(line, kHigh, us)) return;
  p.stage = Dht11Stage::Response;
  if (!detail::dht11WaitWhile(line, kLow, us) || !detail::dht11WaitWhile(line, kHigh, us)) return;
  // Each bit: 50-58 us low, then the high pulse that carries the value.
  p.stage = Dht11Stage::Data;
  for (; p.bits < kDht11Bits; ++p.bits) {
    if (!detail::dht11WaitWhile(line, kLow, us)) return;
    if (!detail::dht11WaitWhile(line, kHigh, p.highUs[p.bits])) return;
  }
  p.stage = Dht11Stage::Done;
}

// What the serial `raw` and `dhtwatch` commands show about the last DHT11 transaction.
struct Dht11Diag {
  bool read = false;  // false until the first transaction
  uint32_t atMs = 0;  // millis() at the end of the transaction
  uint32_t startLowUs = 0;  // measured host start signal (datasheet: 18-30 ms)
  Dht11Stage stage = Dht11Stage::Release;
  uint8_t bits = 0;
  uint8_t bytes[5] = {};  // only when stage == Done
  bool checksumOk = false;
  // High-pulse widths seen for bits read as 0 and as 1; count 0 = no such bit.
  uint8_t zeros = 0, ones = 0;
  uint16_t zeroMinUs = 0, zeroMaxUs = 0, oneMinUs = 0, oneMaxUs = 0;
};

// Fills everything but read, atMs and startLowUs from one walked reply.
Dht11Diag dht11Diagnose(const Dht11Pulses& p);

// One line without a newline, e.g.
//   "bytes=06 00 19 03 22 checksum=ok bit0_us=23..26 bit1_us=69..72 start_us=20105"
//   "error=no_response bits=0 start_us=20105"
// Returns the length, or 0 if cap is too small.
size_t formatDht11Diag(const Dht11Diag& d, char* out, size_t cap);

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
  // Call when a transaction has ended; answered = it returned a valid frame.
  void mark(uint32_t nowMs, bool answered) {
    started_ = true;
    lastMs_ = nowMs;
    if (answered && answered_ < 2) ++answered_;
  }
  // True once two transactions have been answered. The first reply holds the power-up sample and
  // the second the sample taken right after the first start signal, which can still be far off
  // (a DHT11 gave 5 %RH there while later reads were normal); every later reply is settled.
  bool nextReplySettled() const { return answered_ >= 2; }

 private:
  bool started_ = false;
  uint32_t lastMs_ = 0;
  uint8_t answered_ = 0;
};

// Decides whether the temperature/humidity reply read at send time may be published. An SHT31
// reply is always fresh. A DHT11 reply only counts if the DHT11 was already in use at prepare
// time, answered that read (so a new conversion started) and its next reply is settled. A DHT11
// first detected at send time replies with its unsettled power-up sample, so that one is null.
class ClimatePublishGate {
 public:
  // At prepare time. dht: a DHT11 is in use; answered: its read answered; settled:
  // Dht11Throttle::nextReplySettled() after that read.
  void prepared(bool dht, bool answered, bool settled) {
    preparedDht_ = dht;
    primed_ = !dht || (answered && settled);
  }
  // At send time, after the read. dht: a DHT11 is in use now. Consumes the DHT11 priming.
  bool publish(bool dht) {
    bool ok = primed_ && (!dht || preparedDht_);
    primed_ = primed_ && !dht;
    return ok;
  }

 private:
  bool primed_ = false;
  bool preparedDht_ = false;
};

}  // namespace olvar
