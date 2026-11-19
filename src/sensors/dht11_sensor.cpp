#include "dht11_sensor.h"

namespace olvar {

namespace {

constexpr uint32_t kPowerUpMs = 1000;      // datasheet: send no command in the first second
constexpr uint32_t kStartLowMs = 20;       // host start signal, >= 18 ms
constexpr uint32_t kPhaseTimeoutUs = 100;  // every sensor phase is <= 80 us

portMUX_TYPE dhtMux = portMUX_INITIALIZER_UNLOCKED;

// Waits while the pin reads `level`; false if it is still there after kPhaseTimeoutUs.
bool waitWhile(uint8_t pin, int level, uint16_t& us) {
  uint32_t t0 = micros();
  uint32_t dt = 0;
  while (digitalRead(pin) == level) {
    dt = micros() - t0;
    if (dt > kPhaseTimeoutUs) return false;
  }
  us = static_cast<uint16_t>(dt);
  return true;
}

}  // namespace

Dht11Sensor::Dht11Sensor(uint8_t pin) : pin_(pin) {}

void Dht11Sensor::begin() {
  // Open drain: LOW pulls the line down, HIGH releases it to the pull-up. Input stays enabled.
  pinMode(pin_, OUTPUT_OPEN_DRAIN | PULLUP);
  digitalWrite(pin_, HIGH);
  uint32_t now = millis();
  if (now < kPowerUpMs) delay(kPowerUpMs - now);
}

TempHumidity Dht11Sensor::read() {
  uint32_t now = millis();
  if (!throttle_.due(now)) return last_;
  throttle_.mark(now);
  uint8_t bytes[5];
  last_ = readFrame(bytes) ? dht11Decode(bytes) : TempHumidity{};
  return last_;
}

bool Dht11Sensor::readFrame(uint8_t bytes[5]) {
  digitalWrite(pin_, LOW);
  delay(kStartLowMs);

  uint16_t highUs[kDht11Bits];
  uint16_t us = 0;
  bool ok = true;
  portENTER_CRITICAL(&dhtMux);
  digitalWrite(pin_, HIGH);
  // Release (20-40 us high), then the sensor's 80 us low + 80 us high response.
  ok = waitWhile(pin_, HIGH, us) && waitWhile(pin_, LOW, us) && waitWhile(pin_, HIGH, us);
  // Each bit: 50 us low, then high for 26-28 us (0) or ~70 us (1).
  for (size_t i = 0; ok && i < kDht11Bits; ++i) {
    ok = waitWhile(pin_, LOW, us) && waitWhile(pin_, HIGH, highUs[i]);
  }
  portEXIT_CRITICAL(&dhtMux);
  digitalWrite(pin_, HIGH);

  if (ok) dht11PulsesToBytes(highUs, bytes);
  return ok;
}

}  // namespace olvar
