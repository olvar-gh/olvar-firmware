// SHT31 temperature/humidity: single-shot, high repeatability, CRC checked, heater off.
#pragma once

#include <SHT31.h>

#include "i2c_bus.h"
#include "reading.h"

namespace olvar {

class Sht31Sensor {
 public:
  explicit Sht31Sensor(I2cBus& bus, uint8_t addr = pins::kSht31Addr);

  uint8_t address() const { return addr_; }

  // Safe to call when the sensor is absent; read() retries it.
  bool begin();
  // ~15 ms on success. Any I2C or CRC failure gives ok=false for both values.
  TempHumidity read();

 private:
  I2cBus& bus_;
  uint8_t addr_;
  SHT31 sht_;
  bool started_ = false;
};

}  // namespace olvar
