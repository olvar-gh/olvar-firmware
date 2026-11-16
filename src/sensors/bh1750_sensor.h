// BH1750 (GY-302) light sensor in one-time high-resolution mode.
#pragma once

#include <BH1750.h>

#include "i2c_bus.h"
#include "reading.h"

namespace olvar {

class Bh1750Sensor {
 public:
  // Datasheet maximum for H-resolution mode; the typical value is 120 ms.
  static constexpr uint32_t kMeasureMaxMs = 180;

  explicit Bh1750Sensor(I2cBus& bus, uint8_t addr = pins::kBh1750Addr);

  bool begin();
  // Triggers one measurement and waits for it: ~10 ms wake-up + 180 ms, under 200 ms.
  // An I2C failure (negative level from the library) gives ok=false.
  OptF read();

 private:
  I2cBus& bus_;
  BH1750 bh_;
  uint8_t addr_;
  bool started_ = false;
};

}  // namespace olvar
