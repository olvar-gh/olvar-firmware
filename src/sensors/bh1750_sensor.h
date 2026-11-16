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
  // Whole-call budget for read() (PLAN T4: no call blocks longer than 200 ms).
  static constexpr uint32_t kReadBudgetMs = 200;

  explicit Bh1750Sensor(I2cBus& bus, uint8_t addr = pins::kBh1750Addr);

  // Boot-time check through the library (~20 ms of fixed library delays). Optional:
  // read() works without it and recovers a replugged sensor by itself.
  bool begin();
  // One command write + 180 ms wait + one 2-byte read: worst case ~kMeasureMaxMs +
  // I2cBus::kTimeoutMs (a failed write returns at once). A failed transaction gives ok=false.
  OptF read();

 private:
  I2cBus& bus_;
  BH1750 bh_;
  uint8_t addr_;
};

}  // namespace olvar
