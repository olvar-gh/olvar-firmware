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

// The four values cannot be written in one NVS operation, so save() writes them to the staged
// keys first and then sets kKeyStaged, the single write that switches load() over to them. A
// write that fails (or a power cut) before that switch leaves the old values in charge.
bool SettingsStore::load(Settings& out, const Settings& defaults) {
  out = defaults;
  // A read-only open fails with NOT_FOUND until the namespace is first written (fresh flash).
  if (!kv_.begin(kNamespace, true)) return false;
  const bool staged = kv_.getBool(kKeyStaged, false);
  const char* const* keys = staged ? kStagedKeys : kLiveKeys;
  bool any = staged;
  Settings s = defaults;
  if (kv_.getString(keys[0], s.device_id, sizeof(s.device_id))) {
    any = true;
  } else {
    strcpy(s.device_id, defaults.device_id);
  }
  if (kv_.getString(keys[1], s.mqtt_host, sizeof(s.mqtt_host))) {
    any = true;
  } else {
    strcpy(s.mqtt_host, defaults.mqtt_host);
  }
  if (kv_.has(keys[2])) {
    s.mqtt_port = kv_.getU16(keys[2], defaults.mqtt_port);
    any = true;
  }
  if (kv_.getString(keys[3], s.mqtt_pass, sizeof(s.mqtt_pass))) {
    any = true;
  } else {
    strcpy(s.mqtt_pass, defaults.mqtt_pass);
  }
  kv_.end();
  out = s;
  return any;
}

bool SettingsStore::readSet(const char* const keys[4], Settings& s) {
  if (!kv_.has(keys[2])) return false;
  s.mqtt_port = kv_.getU16(keys[2], s.mqtt_port);
  return kv_.getString(keys[0], s.device_id, sizeof(s.device_id)) &&
         kv_.getString(keys[1], s.mqtt_host, sizeof(s.mqtt_host)) &&
         kv_.getString(keys[3], s.mqtt_pass, sizeof(s.mqtt_pass));
}

bool SettingsStore::writeSet(const char* const keys[4], const Settings& s) {
  return kv_.putString(keys[0], s.device_id) && kv_.putString(keys[1], s.mqtt_host) &&
         kv_.putU16(keys[2], s.mqtt_port) && kv_.putString(keys[3], s.mqtt_pass);
}

void SettingsStore::removeStaged() {
  for (int i = 0; i < 4; ++i) kv_.remove(kStagedKeys[i]);
}

bool SettingsStore::save(const Settings& s, const char** err) {
  if (!validate(s, err)) return false;
  if (!kv_.begin(kNamespace, false)) {
    if (err) *err = "NVS could not be opened";
    return false;
  }
  // A leftover switch from an interrupted save means the staged keys are the current settings:
  // move them to the live keys before the staged keys are reused.
  if (kv_.getBool(kKeyStaged, false)) {
    Settings current;
    if (!readSet(kStagedKeys, current) || !writeSet(kLiveKeys, current) || !kv_.remove(kKeyStaged)) {
      kv_.end();
      if (err) *err = "NVS write failed";
      return false;
    }
  }
  if (!writeSet(kStagedKeys, s) || !kv_.putBool(kKeyStaged, true)) {
    kv_.remove(kKeyStaged);
    removeStaged();
    kv_.end();
    if (err) *err = "NVS write failed";
    return false;
  }
  // The new settings are committed. If copying them to the live keys fails, load() keeps
  // reading the staged ones and the next save() finishes the move.
  if (writeSet(kLiveKeys, s) && kv_.remove(kKeyStaged)) removeStaged();
  kv_.end();
  return true;
}

bool SettingsStore::clear() {
  if (!kv_.begin(kNamespace, false)) return false;
  const bool ok = kv_.clear();
  kv_.end();
  return ok;
}

uint32_t SettingsStore::brokerIp(const char* host) {
  if (!kv_.begin(kNamespace, true)) return 0;
  char memo[kBrokerMemoLen];
  const bool ok = kv_.getString(kKeyBrokerIp, memo, sizeof memo);
  kv_.end();
  return ok ? parseBrokerMemo(memo, host) : 0;
}

bool SettingsStore::setBrokerIp(uint32_t ip, const char* host) {
  char memo[kBrokerMemoLen];
  if (!formatBrokerMemo(ip, host, memo, sizeof memo)) return false;
  if (!kv_.begin(kNamespace, false)) return false;
  const bool ok = kv_.putString(kKeyBrokerIp, memo);
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
