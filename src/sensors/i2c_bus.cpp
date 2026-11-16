#include "i2c_bus.h"

namespace olvar {

I2cBus::I2cBus(TwoWire& wire, uint8_t sda, uint8_t scl) : wire_(wire), sda_(sda), scl_(scl) {}

bool I2cBus::begin() {
  bool ok = wire_.begin(sda_, scl_, kClockHz);
  wire_.setTimeOut(kTimeoutMs);
  return ok;
}

bool I2cBus::probe(uint8_t addr) {
  wire_.beginTransmission(addr);
  return wire_.endTransmission() == 0;
}

size_t I2cBus::scan(uint8_t* found, size_t cap) {
  size_t n = 0;
  for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
    if (!probe(addr)) continue;
    if (n < cap) found[n] = addr;
    ++n;
  }
  return n;
}

void I2cBus::reportResult(bool ok) {
  if (ok) {
    failures_ = 0;
    return;
  }
  if (++failures_ >= kFailuresBeforeRecovery) {
    recover();
    failures_ = 0;
  }
}

void I2cBus::recover() {
  wire_.end();
  // Open-drain bit-bang: a stuck slave finishes its byte within 9 clocks and releases SDA.
  pinMode(sda_, INPUT_PULLUP);
  pinMode(scl_, OUTPUT_OPEN_DRAIN);
  for (int i = 0; i < 9; ++i) {
    digitalWrite(scl_, LOW);
    delayMicroseconds(5);
    digitalWrite(scl_, HIGH);
    delayMicroseconds(5);
  }
  // STOP: SDA low -> high while SCL is high.
  pinMode(sda_, OUTPUT_OPEN_DRAIN);
  digitalWrite(sda_, LOW);
  delayMicroseconds(5);
  digitalWrite(scl_, HIGH);
  delayMicroseconds(5);
  digitalWrite(sda_, HIGH);
  delayMicroseconds(5);
  begin();
  ++recoveries_;
}

}  // namespace olvar
