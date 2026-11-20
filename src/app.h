// The helpers above the ARDUINO guard are pure C++ so the native tests can cover them.
#pragma once

#include <stdint.h>

#include "link_record.h"
#include "link_recovery.h"
#include "net/mqtt_link.h"
#include "status_led.h"

namespace olvar {

// A (re)connect sends a reading after kFirstDelayMs instead of waiting out the interval, so the
// gateway sees fresh data within seconds of the device coming back.
class TelemetrySchedule {
 public:
  static constexpr uint32_t kFirstDelayMs = 2000;

  TelemetrySchedule(uint32_t periodMs, uint32_t nowMs) : periodMs_(periodMs), next_(nowMs + periodMs) {}

  bool due(uint32_t nowMs, bool mqttConnected) {
    if (mqttConnected && !wasConnected_) next_ = nowMs + kFirstDelayMs;
    wasConnected_ = mqttConnected;
    if (static_cast<int32_t>(nowMs - next_) < 0) return false;
    next_ += periodMs_;
    // After a stall longer than one period, skip the missed slots rather than firing in a burst.
    if (static_cast<int32_t>(nowMs - next_) >= 0) next_ = nowMs + periodMs_;
    return true;
  }

 private:
  uint32_t periodMs_;
  uint32_t next_;
  bool wasConnected_ = false;
};

inline LedMode ledModeFor(bool wifiUp, bool mqttUp) {
  if (!wifiUp) return LedMode::NoWifi;
  return mqttUp ? LedMode::Online : LedMode::NoMqtt;
}

inline const char* mqttStateName(MqttSession::State s) {
  switch (s) {
    case MqttSession::State::Waiting: return "disconnected";
    case MqttSession::State::Connecting: return "connecting";
    case MqttSession::State::Connected: return "connected";
  }
  return "unknown";
}

}  // namespace olvar

#ifdef ARDUINO

#include "net/provisioning.h"
#include "net/wifi_link.h"
#include "sensors/bh1750_sensor.h"
#include "sensors/climate_sensor.h"
#include "sensors/i2c_bus.h"
#include "sensors/soil_sensor.h"
#include "serial_cli.h"
#include "settings.h"
#include "storage/settings_store.h"

namespace olvar {

class App {
 public:
  App();

  void setup();
  void loop();

 private:
  Reading readSensors();
  void sendTelemetry();
  void fillStatus(CliStatus& s);
  void recover();
  // Copies the links' state into the RTC reason record.
  void updateLinkRecord();
  void printLinkRecord(const LinkRecord& r, const char* indent, char* out, size_t cap);
  [[noreturn]] void restart(RestartCause cause);

  I2cBus bus_;
  ClimateSensor climate_;
  Bh1750Sensor bh_;
  SoilSensor soil_;
  NvsKvStore kv_;
  SettingsStore store_;
  BootButton button_;
  WifiLink wifi_;
  MqttLink mqtt_;
  StatusLed led_;
  SerialCli console_;
  TelemetrySchedule schedule_;
  LinkRecovery recovery_;
  uint32_t wifiUps_ = 0;
  uint32_t recordAtMs_ = 0;
  // What the boot before this one left in RTC memory.
  LinkRecord prevRecord_ = {};
  bool prevRecordValid_ = false;
  // `status` output of the reason records; long, so not on the stack.
  char linkText_[1200] = {};
  // A due message waits here until sendAtMs_ while a DHT11 converts a fresh sample (~2 s).
  bool sendPending_ = false;
  uint32_t sendAtMs_ = 0;
  Settings settings_;
  char bootId_[9] = {};
};

}  // namespace olvar

#endif  // ARDUINO
