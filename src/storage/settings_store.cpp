#include "settings_store.h"

#include <string.h>

#include "ids.h"

namespace olvar {

namespace {

template <size_t N>
void copyIfSet(char (&dst)[N], const char* src) {
  if (src && src[0] && strlen(src) < N) strcpy(dst, src);
}

}  // namespace

Settings defaultSettings(const uint8_t mac[6]) {
  Settings s;
  deviceIdFromMac(mac, s.device_id);
#ifdef OLVAR_DEFAULT_DEVICE_ID
  copyIfSet(s.device_id, OLVAR_DEFAULT_DEVICE_ID);
#endif
#ifdef OLVAR_DEFAULT_MQTT_HOST
  copyIfSet(s.mqtt_host, OLVAR_DEFAULT_MQTT_HOST);
#endif
#ifdef OLVAR_DEFAULT_MQTT_PORT
  if (OLVAR_DEFAULT_MQTT_PORT > 0 && OLVAR_DEFAULT_MQTT_PORT <= 65535) s.mqtt_port = OLVAR_DEFAULT_MQTT_PORT;
#endif
#ifdef OLVAR_DEFAULT_MQTT_PASS
  copyIfSet(s.mqtt_pass, OLVAR_DEFAULT_MQTT_PASS);
#endif
  return s;
}

bool SettingsStore::load(Settings& out, const Settings& defaults) {
  out = defaults;
  // A read-only open fails with NOT_FOUND until the namespace is first written (fresh flash).
  if (!kv_.begin(kNamespace, true)) return false;
  bool any = false;
  Settings s = defaults;
  if (kv_.getString(kKeyDeviceId, s.device_id, sizeof(s.device_id))) {
    any = true;
  } else {
    strcpy(s.device_id, defaults.device_id);
  }
  if (kv_.getString(kKeyMqttHost, s.mqtt_host, sizeof(s.mqtt_host))) {
    any = true;
  } else {
    strcpy(s.mqtt_host, defaults.mqtt_host);
  }
  if (kv_.has(kKeyMqttPort)) {
    s.mqtt_port = kv_.getU16(kKeyMqttPort, defaults.mqtt_port);
    any = true;
  }
  if (kv_.getString(kKeyMqttPass, s.mqtt_pass, sizeof(s.mqtt_pass))) {
    any = true;
  } else {
    strcpy(s.mqtt_pass, defaults.mqtt_pass);
  }
  kv_.end();
  out = s;
  return any;
}

bool SettingsStore::save(const Settings& s, const char** err) {
  if (!validate(s, err)) return false;
  if (!kv_.begin(kNamespace, false)) {
    if (err) *err = "NVS could not be opened";
    return false;
  }
  const bool ok = kv_.putString(kKeyDeviceId, s.device_id) && kv_.putString(kKeyMqttHost, s.mqtt_host) &&
                  kv_.putU16(kKeyMqttPort, s.mqtt_port) && kv_.putString(kKeyMqttPass, s.mqtt_pass);
  kv_.end();
  if (!ok && err) *err = "NVS write failed";
  return ok;
}

bool SettingsStore::clear() {
  if (!kv_.begin(kNamespace, false)) return false;
  const bool ok = kv_.clear();
  kv_.end();
  return ok;
}

bool SettingsStore::portalRequested() {
  if (!kv_.begin(kNamespace, true)) return false;
  const bool on = kv_.getBool(kKeyPortal, false);
  kv_.end();
  return on;
}

bool SettingsStore::setPortalRequested(bool on) {
  if (!kv_.begin(kNamespace, false)) return false;
  const bool ok = on ? kv_.putBool(kKeyPortal, true) : (!kv_.has(kKeyPortal) || kv_.remove(kKeyPortal));
  kv_.end();
  return ok;
}

#ifdef ARDUINO
bool NvsKvStore::getString(const char* key, char* out, size_t cap) {
  // Preferences logs an error for every missing key; check first to keep the boot log clean.
  if (!prefs_.isKey(key)) return false;
  return prefs_.getString(key, out, cap) > 0;
}

bool NvsKvStore::putString(const char* key, const char* value) {
  // Preferences returns strlen(value), so an empty string would look like a failure.
  if (!value[0]) return prefs_.putString(key, value) == 0 && prefs_.isKey(key);
  return prefs_.putString(key, value) == strlen(value);
}
#endif

}  // namespace olvar
