// Persistent Settings in NVS (Preferences namespace "olvar"). The store logic talks to a KvStore so
// it also builds in native tests; NvsKvStore is the on-board backend.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "link_recovery.h"
#include "settings.h"

namespace olvar {

class KvStore {
 public:
  virtual ~KvStore() = default;
  virtual bool begin(const char* ns, bool readOnly) = 0;
  virtual void end() = 0;
  virtual bool has(const char* key) = 0;
  // Copies the value with its NUL into out; returns false if missing or it doesn't fit in cap.
  virtual bool getString(const char* key, char* out, size_t cap) = 0;
  virtual bool putString(const char* key, const char* value) = 0;
  virtual uint16_t getU16(const char* key, uint16_t fallback) = 0;
  virtual bool putU16(const char* key, uint16_t value) = 0;
  virtual bool getBool(const char* key, bool fallback) = 0;
  virtual bool putBool(const char* key, bool value) = 0;
  virtual bool remove(const char* key) = 0;
  virtual bool clear() = 0;
};

// Built-in defaults (device_id from the MAC, port 1883), overridden by the OLVAR_DEFAULT_* build
// flags from secrets.ini when they are set and non-empty.
Settings defaultSettings(const uint8_t mac[6]);

class SettingsStore {
 public:
  static constexpr const char* kNamespace = "olvar";
  static constexpr const char* kKeyDeviceId = "dev_id";
  static constexpr const char* kKeyMqttHost = "mq_host";
  static constexpr const char* kKeyMqttPort = "mq_port";
  static constexpr const char* kKeyMqttPass = "mq_pass";
  static constexpr const char* kKeyPortal = "portal";
  static constexpr const char* kKeyStaged = "n_commit";
  // "<ip> <mq_host>": the last address mq_host resolved to that accepted an MQTT connection.
  static constexpr const char* kKeyBrokerIp = "mq_last";

  explicit SettingsStore(KvStore& kv) : kv_(kv) {}

  // Every key missing from NVS (or unreadable) takes its value from `defaults`.
  // Returns true if any key came from NVS.
  bool load(Settings& out, const Settings& defaults);
  // Refuses settings that fail validate(); *err then names the rule.
  bool save(const Settings& s, const char** err = nullptr);
  bool clear();

  bool portalRequested();

  // The remembered broker address for this host (0 if none, or learnt for another host).
  uint32_t brokerIp(const char* host);
  bool setBrokerIp(uint32_t ip, const char* host);
  bool setPortalRequested(bool on);

 private:
  static constexpr const char* kLiveKeys[4] = {kKeyDeviceId, kKeyMqttHost, kKeyMqttPort, kKeyMqttPass};
  static constexpr const char* kStagedKeys[4] = {"n_dev_id", "n_mq_host", "n_mq_port", "n_mq_pass"};

  bool readSet(const char* const keys[4], Settings& s);
  bool writeSet(const char* const keys[4], const Settings& s);
  void removeStaged();

  KvStore& kv_;
};

}  // namespace olvar

#ifdef ARDUINO
#include <Preferences.h>

namespace olvar {

class NvsKvStore : public KvStore {
 public:
  bool begin(const char* ns, bool readOnly) override { return prefs_.begin(ns, readOnly); }
  void end() override { prefs_.end(); }
  bool has(const char* key) override { return prefs_.isKey(key); }
  bool getString(const char* key, char* out, size_t cap) override;
  bool putString(const char* key, const char* value) override;
  uint16_t getU16(const char* key, uint16_t fallback) override { return prefs_.getUShort(key, fallback); }
  bool putU16(const char* key, uint16_t value) override { return prefs_.putUShort(key, value) == sizeof(value); }
  bool getBool(const char* key, bool fallback) override { return prefs_.getBool(key, fallback); }
  bool putBool(const char* key, bool value) override { return prefs_.putBool(key, value) == 1; }
  bool remove(const char* key) override { return prefs_.remove(key); }
  bool clear() override { return prefs_.clear(); }

 private:
  Preferences prefs_;
};

}  // namespace olvar
#endif  // ARDUINO
