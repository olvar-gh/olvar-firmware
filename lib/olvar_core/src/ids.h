// Device/boot identifiers and MQTT topic names (docs/PLAN.md §4.1).
// Pure C++17, no Arduino.h, so it builds in native tests.
#pragma once

#include <stddef.h>
#include <stdint.h>

// Gateway rule ^[a-z0-9][a-z0-9-]{2,31}$, and "olvar-gateway" is reserved.
// The id is also the MQTT client id, username and topic segment.
bool isValidDeviceId(const char* id);

// "olvar-" + last 3 MAC bytes as lowercase hex, e.g. "olvar-a1b2c3".
void deviceIdFromMac(const uint8_t mac[6], char out[13]);

// 8 lowercase hex chars, zero-padded (e.g. from esp_random()).
void bootIdFromRandom(uint32_t r, char out[9]);

// "olvar/v1/<id>/telemetry" and "olvar/v1/<id>/status".
// Return the length written (without NUL), or 0 if id is null or cap is too small.
size_t telemetryTopic(const char* id, char* out, size_t cap);
size_t statusTopic(const char* id, char* out, size_t cap);
