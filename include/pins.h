// Board wiring and fixed timing for the 38-pin ESP32-WROOM-32D (see docs/PLAN.md §7.4).
// Pure C++, no Arduino.h, so native tests can include it.
#pragma once

#include <stdint.h>

namespace olvar {
namespace pins {

constexpr uint8_t kI2cSda = 21;   // shared I2C bus: SHT31 + BH1750
constexpr uint8_t kI2cScl = 22;
constexpr uint8_t kSoil = 34;     // ADC1_CH6, input-only; 100 nF to GND recommended
constexpr uint8_t kDht11 = 4;     // DHT11 DATA, used only when no SHT31 answers; 10k pull-up to 3V3
constexpr uint8_t kLed = 2;       // on-board status LED
constexpr uint8_t kBoot = 0;      // BOOT button, read only at runtime (strapping pin)

constexpr uint8_t kSht31Addr = 0x44;     // ADDR low
constexpr uint8_t kSht31AltAddr = 0x45;  // ADDR high
constexpr uint8_t kBh1750Addr = 0x23;

}  // namespace pins
}  // namespace olvar

// Telemetry interval in seconds (fixed by the contract, decision E).
#ifndef OLVAR_INTERVAL_S
#define OLVAR_INTERVAL_S 60
#endif
