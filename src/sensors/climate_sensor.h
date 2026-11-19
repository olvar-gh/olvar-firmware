// Temperature/humidity from whichever sensor is fitted: an SHT31 at 0x44 or 0x45 on the I2C
// bus, else a DHT11 on GPIO4. The first one that answers is kept until the next reboot.
#pragma once

#include "dht11_sensor.h"
#include "i2c_bus.h"
#include "reading.h"
#include "sht31_sensor.h"

namespace olvar {

class ClimateSensor {
 public:
  enum class Kind : uint8_t { None, Sht31, Dht11 };

  explicit ClimateSensor(I2cBus& bus);

  // Probes SHT31 0x44, SHT31 0x45, then the DHT11. False when none answers; read() then keeps
  // probing on every call and returns null until one does.
  bool begin();
  TempHumidity read();

  Kind kind() const { return kind_; }
  // "SHT31 at 0x44", "SHT31 at 0x45", "DHT11 on GPIO4" or "none".
  const char* name() const { return name_; }

 private:
  bool detect();

  I2cBus& bus_;
  Sht31Sensor sht44_, sht45_;
  Dht11Sensor dht_;
  Sht31Sensor* sht_ = nullptr;
  Kind kind_ = Kind::None;
  char name_[16] = "none";
};

}  // namespace olvar
