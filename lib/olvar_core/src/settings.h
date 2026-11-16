// Device settings entered in the portal and stored in NVS (see T5).
// Pure C++17, no Arduino.h, so it builds in native tests.
#pragma once

#include <stdint.h>

// The MQTT username is always device_id (gateway ACL: username == client id == device_id),
// so there is no separate username field.
struct Settings {
  char device_id[33] = {};
  char mqtt_host[64] = {};
  uint16_t mqtt_port = 1883;
  char mqtt_pass[65] = {};
};

// Returns true when every field is usable. On failure, *err (if err is non-null) points to a
// static message naming the broken rule, suitable for showing in the portal.
bool validate(const Settings& s, const char** err);
