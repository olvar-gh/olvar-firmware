// Shared data types for one sensor reading and its metadata.
// Pure C++17, no Arduino.h, so it builds in native tests.
#pragma once

#include <stdint.h>

// Optional values: ok=false means "read failed" and is serialised as JSON null.
struct OptF { bool ok = false; float v = 0; };
struct OptI { bool ok = false; int32_t v = 0; };

struct Reading {
  OptF temperature_c, humidity_pct, lux;
  OptI soil_raw;
};

struct Meta {
  const char* device_id;
  const char* boot_id;
  uint32_t seq;
  uint32_t uptime_ms;
  const char* fw;
  OptI rssi_dbm;
};
