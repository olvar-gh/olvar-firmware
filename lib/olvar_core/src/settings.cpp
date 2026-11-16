#include "settings.h"

#include <string.h>

namespace {

// Charset/length part of isValidDeviceId() in ids.h (T2): ^[a-z0-9][a-z0-9-]{2,31}$.
// validate() checks the reserved id separately so it can report that rule by name.
// Kept local so settings.cpp doesn't depend on ids.* at link time.
bool deviceIdOk(const char* id, size_t len) {
  if (len < 3 || len > 32) return false;
  for (size_t i = 0; i < len; ++i) {
    const char c = id[i];
    const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (!alnum && (i == 0 || c != '-')) return false;
  }
  return true;
}

bool fail(const char** err, const char* msg) {
  if (err) *err = msg;
  return false;
}

}  // namespace

bool validate(const Settings& s, const char** err) {
  // strnlen == sizeof means the buffer has no terminator: treat as invalid, never read past it.
  const size_t idLen = strnlen(s.device_id, sizeof(s.device_id));
  if (idLen == sizeof(s.device_id) || !deviceIdOk(s.device_id, idLen)) {
    return fail(err, "device_id must be 3-32 chars of a-z, 0-9 or '-', starting with a letter or digit");
  }
  if (strcmp(s.device_id, "olvar-gateway") == 0) {
    return fail(err, "device_id 'olvar-gateway' is reserved");
  }
  const size_t hostLen = strnlen(s.mqtt_host, sizeof(s.mqtt_host));
  if (hostLen == 0) return fail(err, "MQTT host must not be empty");
  if (hostLen == sizeof(s.mqtt_host)) return fail(err, "MQTT host must be at most 63 chars");
  if (s.mqtt_port == 0) return fail(err, "MQTT port must be 1-65535");
  const size_t passLen = strnlen(s.mqtt_pass, sizeof(s.mqtt_pass));
  if (passLen == 0) return fail(err, "MQTT password must not be empty");
  if (passLen == sizeof(s.mqtt_pass)) return fail(err, "MQTT password must be at most 64 chars");
  if (err) *err = nullptr;
  return true;
}
