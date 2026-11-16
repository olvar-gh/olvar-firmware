#include "sht31_sensor.h"

#include <math.h>

namespace olvar {

Sht31Sensor::Sht31Sensor(I2cBus& bus, uint8_t addr) : bus_(bus), sht_(addr, &bus.wire()) {}

bool Sht31Sensor::begin() {
  started_ = sht_.begin() && sht_.heatOff();
  return started_;
}

Sht31Result Sht31Sensor::read() {
  Sht31Result r;
  if (!started_ && !begin()) {
    bus_.reportResult(false);
    return r;
  }
  // fast=false: high-repeatability command (0x2400, no clock stretching) and CRC check.
  bool ok = sht_.read(false);
  bus_.reportResult(ok);
  if (!ok) {
    started_ = false;  // re-init after a replug or bus recovery
    return r;
  }
  float t = sht_.getTemperature();
  float h = sht_.getHumidity();
  r.temperature_c = {isfinite(t), t};
  r.humidity_pct = {isfinite(h), h};
  return r;
}

}  // namespace olvar
