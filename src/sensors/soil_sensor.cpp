#include "soil_sensor.h"

// trimmedMean() comes from lib/olvar_core/src/adc_filter.*. The private copy below keeps the
// adapter building on its own when that header isn't on the include path.
#if __has_include("adc_filter.h")
#include "adc_filter.h"
#else
#include <algorithm>
namespace {
int32_t trimmedMean(int32_t* samples, size_t n, size_t trimEachSide) {
  if (n == 0) return 0;
  std::sort(samples, samples + n);
  if (n == 1 || trimEachSide * 2 >= n) return samples[n / 2];
  int64_t sum = 0;
  for (size_t i = trimEachSide; i < n - trimEachSide; ++i) sum += samples[i];
  return static_cast<int32_t>(sum / static_cast<int64_t>(n - 2 * trimEachSide));
}
}  // namespace
#endif

namespace olvar {

SoilSensor::SoilSensor(uint8_t pin) : pin_(pin) {}

void SoilSensor::begin() {
  analogReadResolution(12);
  analogSetPinAttenuation(pin_, ADC_11db);
}

OptI SoilSensor::sampleRaw() {
  OptI r;
  if (digitalPinToAnalogChannel(pin_) < 0) return r;
  int32_t samples[kSamples];
  for (size_t i = 0; i < kSamples; ++i) {
    if (i) delayMicroseconds(kSampleSpacingUs);
    samples[i] = analogRead(pin_);
  }
  int32_t raw = trimmedMean(samples, kSamples, kTrimEachSide);
  if (raw < 0 || raw > 4095) return r;
  r = {true, raw};
  return r;
}

}  // namespace olvar
