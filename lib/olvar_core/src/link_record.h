// The link reason record: a small snapshot of the network state and counters that survives a
// restart (it lives in RTC memory that a software reset, panic or watchdog reset leaves alone),
// so the boot after an outage can say why the previous one ended. Never part of telemetry.
// Pure C++17, no Arduino.h, so it builds in native tests.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace olvar {

// Why the firmware restarted itself. None means it did not: power loss, a crash, a watchdog or
// a reset button; the reset reason of the next boot tells which.
enum class RestartCause : uint8_t { None, Supervisor, Command, Portal };

constexpr uint8_t kNoReason = 0xFF;  // wifiReason / mqttReason: nothing recorded yet

struct LinkRecord {
  uint32_t magic;
  uint8_t version;
  uint8_t resetReason;  // esp_reset_reason_t of the boot this record describes
  uint8_t cause;        // RestartCause
  uint8_t wifiStatus;   // wl_status_t when last updated
  int8_t rssi;          // dBm when last updated, 0 if not connected
  uint8_t wifiReason;   // last Wi-Fi disconnect reason (wifi_err_reason_t)
  uint8_t mqttReason;   // last espMqttClient DisconnectReason
  uint8_t resolveFailing;  // 1 while the broker name does not resolve
  uint32_t uptimeS;
  uint32_t sinceAckS;      // seconds since the last acknowledged telemetry message
  uint32_t wifiDrops;      // Wi-Fi disconnect events
  uint32_t wifiRetries;    // reconnects asked for after kDownMs without Wi-Fi
  uint32_t wifiResets;     // forced Wi-Fi reconnects of the recovery ladder
  uint32_t mqttConnects;   // accepted MQTT connections
  uint32_t mqttDrops;      // MQTT sessions lost or dropped
  uint32_t ackTimeouts;    // MQTT sessions dropped because a PUBACK did not come
  uint32_t resolveFails;   // failed broker name lookups
  uint32_t fallbacks;      // connects to the remembered address after a failed lookup
  uint32_t restartChain;   // supervisor restarts in a row before this boot
  uint32_t check;
};

constexpr uint32_t kLinkRecordMagic = 0x4C4B5231;  // "LKR1"
constexpr uint8_t kLinkRecordVersion = 1;

uint32_t linkRecordChecksum(const LinkRecord& r);
void sealLinkRecord(LinkRecord& r);
bool linkRecordValid(const LinkRecord& r);

// A fresh record for this boot. prev is what the last boot left behind, or null when the RTC
// memory held nothing valid (first power-up).
LinkRecord newBootRecord(const LinkRecord* prev, uint8_t resetReason);

const char* restartCauseName(uint8_t cause);
const char* mqttReasonName(uint8_t reason);

// Names the caller looks up on the board (the Arduino core owns these tables).
struct LinkRecordNames {
  const char* resetReason;
  const char* wifiStatus;
  const char* wifiReason;
};

// Multi-line text for the `status` command and the boot banner, every line starting with
// `indent`. Returns false if it was cut short.
bool formatLinkRecord(const LinkRecord& r, const LinkRecordNames& names, const char* indent, char* out,
                      size_t cap);

}  // namespace olvar
