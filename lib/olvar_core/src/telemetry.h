// Telemetry JSON payload (docs/PLAN.md §4.1, fixture docs/contract/telemetry.example.json).
// Pure C++17, no Arduino.h, so it builds in native tests.
#pragma once

#include <stddef.h>

#include "reading.h"

// Writes one NUL-terminated telemetry object into out and returns its length
// (without NUL). Keys: device_id, boot_id, seq, uptime_ms, temperature_c,
// humidity_pct, lux, soil_raw, [climate_sensor only when a sensor was found],
// [rssi_dbm only when ok], fw. Failed (ok=false)
// or non-finite values are null; temperature/humidity have 2 dp, lux 1 dp.
// Returns 0 (and writes nothing usable) when cap is too small or a Meta string is null.
size_t buildTelemetryJson(const Reading& r, const Meta& m, char* out, size_t cap);

// "sht31", "dht11", or nullptr for ClimateKind::None (the key is then omitted).
const char* climateSensorId(ClimateKind k);
