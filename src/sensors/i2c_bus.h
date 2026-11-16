// Shared I2C bus (SHT31 + BH1750) on GPIO21/22 at 100 kHz, with bus recovery.
#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "pins.h"

namespace olvar {

class I2cBus {
 public:
  static constexpr uint32_t kClockHz = 100000;
  static constexpr uint8_t kFailuresBeforeRecovery = 3;
  static constexpr uint16_t kTimeoutMs = 50;  // per transaction; keeps every read < 200 ms

  explicit I2cBus(TwoWire& wire = Wire, uint8_t sda = pins::kI2cSda, uint8_t scl = pins::kI2cScl);

  bool begin();
  TwoWire& wire() { return wire_; }

  // Writes the 7-bit addresses (0x08..0x77) that ACK into `found`; returns how many ACKed
  // (may exceed `cap`, only the first `cap` are stored).
  size_t scan(uint8_t* found, size_t cap);
  bool probe(uint8_t addr);

  // Sensors report every transaction. After kFailuresBeforeRecovery consecutive failures
  // the bus is recovered once and the counter restarts.
  void reportResult(bool ok);
  // Releases a slave stuck holding SDA low: 9 SCL pulses, a STOP, then Wire.end()/begin().
  void recover();

  uint8_t consecutiveFailures() const { return failures_; }
  uint32_t recoveries() const { return recoveries_; }

 private:
  TwoWire& wire_;
  uint8_t sda_, scl_;
  uint8_t failures_ = 0;
  uint32_t recoveries_ = 0;
};

}  // namespace olvar
