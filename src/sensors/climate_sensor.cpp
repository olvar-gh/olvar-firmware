#include "climate_sensor.h"

#include <stdio.h>

namespace olvar {

ClimateSensor::ClimateSensor(I2cBus& bus)
    : bus_(bus), sht44_(bus, pins::kSht31Addr), sht45_(bus, pins::kSht31AltAddr) {}

bool ClimateSensor::begin() {
  dht_.begin();
  return detect();
}

TempHumidity ClimateSensor::read() {
  if (kind_ == Kind::None && !detect()) return {};
  // Right after detect() the DHT11 is inside its 2 s window, so this returns the probe's read.
  return kind_ == Kind::Sht31 ? sht_->read() : dht_.read();
}

uint32_t ClimateSensor::prepare() {
  if (kind_ == Kind::None) detect();
  if (kind_ != Kind::Dht11) return 0;
  dht_.read();  // replies with the old sample and starts a new conversion (no-op inside 2 s)
  return dht_.msUntilDue();
}

bool ClimateSensor::detect() {
  for (Sht31Sensor* s : {&sht44_, &sht45_}) {
    // probe() first: a plain ACK check that doesn't count as a failed read on the bus.
    if (bus_.probe(s->address()) && s->begin()) {
      sht_ = s;
      kind_ = Kind::Sht31;
      snprintf(name_, sizeof(name_), "SHT31 at 0x%02X", s->address());
      return true;
    }
  }
  if (dht_.read().temperature_c.ok) {
    kind_ = Kind::Dht11;
    snprintf(name_, sizeof(name_), "DHT11 on GPIO%u", static_cast<unsigned>(pins::kDht11));
    return true;
  }
  return false;
}

}  // namespace olvar
