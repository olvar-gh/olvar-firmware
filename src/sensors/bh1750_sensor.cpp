#include "bh1750_sensor.h"

#include <math.h>

namespace olvar {

Bh1750Sensor::Bh1750Sensor(I2cBus& bus, uint8_t addr) : bus_(bus), bh_(addr), addr_(addr) {}

bool Bh1750Sensor::begin() {
  started_ = bh_.begin(BH1750::ONE_TIME_HIGH_RES_MODE, addr_, &bus_.wire());
  return started_;
}

OptF Bh1750Sensor::read() {
  OptF r;
  if (!started_ && !begin()) {
    bus_.reportResult(false);
    return r;
  }
  // One-time mode powers down after each measurement, so every read re-arms it.
  // configure() includes the 10 ms wake-up and records the start time.
  if (!bh_.configure(BH1750::ONE_TIME_HIGH_RES_MODE)) {
    bus_.reportResult(false);
    started_ = false;
    return r;
  }
  uint32_t start = millis();
  while (!bh_.measurementReady(true) && millis() - start < kMeasureMaxMs) delay(1);
  float lux = bh_.readLightLevel();
  bool ok = lux >= 0 && isfinite(lux);
  bus_.reportResult(ok);
  if (!ok) {
    started_ = false;
    return r;
  }
  r = {true, lux};
  return r;
}

}  // namespace olvar
