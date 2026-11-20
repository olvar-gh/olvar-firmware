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

  // Releases the line to the pull-up.
  void begin();
  // ~25 ms when it talks to the sensor: a 20 ms start pulse (delay(), other tasks keep running)
  // and <= 5 ms of bit timing with interrupts off on this core only. The first call waits until
  // 2 s of uptime (sensor power-up). Within 2 s of the end of the previous transaction it returns
  // that attempt's result without touching the pin. A timeout or a bad checksum gives ok=false.
  // The DHT11 replies with the sample it took after the previous transaction and then measures
  // again, so a current value needs two reads 2 s apart (ClimateSensor::prepare()).
  TempHumidity read();
  uint32_t dueAtMs() const { return throttle_.dueAtMs(millis()); }
  // False until two reads have been answered: the replies before that may be unsettled.
  bool nextReplySettled() const { return throttle_.nextReplySettled(); }
  // Raw bytes, checksum and pulse widths of the last transaction (serial `raw`, `dhtwatch`).
  const Dht11Diag& diag() const { return diag_; }

 private:
  bool readFrame(uint8_t bytes[5]);

  uint8_t pin_;
  Dht11Throttle throttle_;
  TempHumidity last_;
  Dht11Diag diag_;
};

}  // namespace olvar
