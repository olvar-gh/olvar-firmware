// DHT11 temperature/humidity on one GPIO (bare 4-pin ASAIR part, 10k pull-up to 3V3).
#pragma once

#include <Arduino.h>

#include "dht11.h"
#include "pins.h"
#include "reading.h"

namespace olvar {

class Dht11Sensor {
 public:
  explicit Dht11Sensor(uint8_t pin = pins::kDht11);

  // Releases the line and waits out the sensor's first second after power-up.
  void begin();
  // ~25 ms when it talks to the sensor: a 20 ms start pulse (delay(), other tasks keep running)
  // and <= 5 ms of bit timing with interrupts off on this core only. Within 2 s of the previous
  // attempt it returns that attempt's result without touching the pin. A timeout or a bad
  // checksum gives ok=false for both values.
  TempHumidity read();

 private:
  bool readFrame(uint8_t bytes[5]);

  uint8_t pin_;
  Dht11Throttle throttle_;
  TempHumidity last_;
};

}  // namespace olvar
