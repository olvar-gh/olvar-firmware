#include "dht11_sensor.h"

namespace olvar {

namespace {

constexpr uint32_t kPowerUpMs = 2000;  // datasheet §5.4: no start signal in the first 2 s
constexpr uint32_t kStartLowMs = 20;   // host start signal, 18-30 ms

portMUX_TYPE dhtMux = portMUX_INITIALIZER_UNLOCKED;

// The bus as dht11WalkReply() sees it.
struct PinLine {
  uint8_t pin;
  int level() { return digitalRead(pin); }
  uint32_t us() { return micros(); }
};

}  // namespace

Dht11Sensor::Dht11Sensor(uint8_t pin) : pin_(pin) {}

void Dht11Sensor::begin() {
  // Open drain: LOW pulls the line down, HIGH releases it to the pull-up. Input stays enabled.
  pinMode(pin_, OUTPUT_OPEN_DRAIN | PULLUP);
  digitalWrite(pin_, HIGH);
}

TempHumidity Dht11Sensor::read() {
  uint32_t now = millis();
  if (now < kPowerUpMs) delay(kPowerUpMs - now);
  if (!throttle_.due(millis())) return last_;
  uint8_t bytes[5];
  last_ = readFrame(bytes) ? dht11Decode(bytes) : TempHumidity{};
  throttle_.mark(millis(), last_.temperature_c.ok);  // the 2 s count from the end of the transaction
  return last_;
}

bool Dht11Sensor::readFrame(uint8_t bytes[5]) {
  uint32_t t0 = micros();
  digitalWrite(pin_, LOW);
  delay(kStartLowMs);

  Dht11Pulses pulses;
  PinLine line{pin_};
  portENTER_CRITICAL(&dhtMux);
  digitalWrite(pin_, HIGH);
  uint32_t startLowUs = micros() - t0;
  dht11WalkReply(line, pulses);
  portEXIT_CRITICAL(&dhtMux);
  digitalWrite(pin_, HIGH);

  diag_ = dht11Diagnose(pulses);
  diag_.read = true;
  diag_.atMs = millis();
  diag_.startLowUs = startLowUs;
  if (pulses.stage != Dht11Stage::Done) return false;
  for (size_t i = 0; i < 5; ++i) bytes[i] = diag_.bytes[i];
  return true;
}

}  // namespace olvar
