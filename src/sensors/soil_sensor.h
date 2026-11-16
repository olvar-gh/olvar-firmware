// Capacitive soil moisture sensor on GPIO34 (ADC1_CH6). Reports the raw 12-bit count,
// higher = drier. No calibration on the device (contract decision D).
#pragma once

#include <Arduino.h>

#include "pins.h"
#include "reading.h"

namespace olvar {

class SoilSensor {
 public:
  static constexpr size_t kSamples = 64;
  static constexpr size_t kTrimEachSide = 16;  // average the middle 32
  static constexpr uint32_t kSampleSpacingUs = 1000;

  explicit SoilSensor(uint8_t pin = pins::kSoil);

  // 12-bit resolution, 11 dB attenuation (~150-3100 mV range).
  void begin();
  // 64 samples ~1 ms apart (~65 ms), trimmed mean. ok=false if the pin has no ADC channel.
  OptI sampleRaw();

 private:
  uint8_t pin_;
};

}  // namespace olvar
