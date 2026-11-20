#include "link_record.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace olvar {

uint32_t linkRecordChecksum(const LinkRecord& r) {
  // FNV-1a over every byte before `check`.
  const auto* p = reinterpret_cast<const uint8_t*>(&r);
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < offsetof(LinkRecord, check); ++i) {
    h ^= p[i];
    h *= 16777619u;
  }
  return h;
}

void sealLinkRecord(LinkRecord& r) { r.check = linkRecordChecksum(r); }

bool linkRecordValid(const LinkRecord& r) {
  return r.magic == kLinkRecordMagic && r.version == kLinkRecordVersion && r.check == linkRecordChecksum(r);
}

LinkRecord newBootRecord(const LinkRecord* prev, uint8_t resetReason) {
  LinkRecord r;
  memset(&r, 0, sizeof r);  // padding too, so the checksum is deterministic
  r.magic = kLinkRecordMagic;
  r.version = kLinkRecordVersion;
  r.resetReason = resetReason;
  r.wifiReason = kNoReason;
  r.mqttReason = kNoReason;
  if (prev != nullptr && prev->cause == static_cast<uint8_t>(RestartCause::Supervisor)) {
    r.restartChain = prev->restartChain + 1;
  }
  sealLinkRecord(r);
  return r;
}

const char* restartCauseName(uint8_t cause) {
  switch (static_cast<RestartCause>(cause)) {
    case RestartCause::None: return "not restarted by the firmware";
    case RestartCause::Supervisor: return "supervisor restart (no acknowledged telemetry)";
    case RestartCause::Command: return "reboot command";
    case RestartCause::Portal: return "setup portal requested";
  }
  return "unknown";
}

const char* mqttReasonName(uint8_t reason) {
  // espMqttClientTypes::DisconnectReason
  switch (reason) {
    case 0: return "USER_OK";
    case 1: return "MQTT_UNACCEPTABLE_PROTOCOL_VERSION";
    case 2: return "MQTT_IDENTIFIER_REJECTED";
    case 3: return "MQTT_SERVER_UNAVAILABLE";
    case 4: return "MQTT_MALFORMED_CREDENTIALS";
    case 5: return "MQTT_NOT_AUTHORIZED";
    case 6: return "TLS_BAD_FINGERPRINT";
    case 7: return "TCP_DISCONNECTED";
    case kNoReason: return "none";
  }
  return "unknown";
}

namespace {

struct Writer {
  char* out;
  size_t cap;
  size_t len = 0;
  bool ok = true;

  void add(const char* fmt, ...) {
    if (!ok) return;
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(out + len, cap - len, fmt, ap);
    va_end(ap);
    if (n < 0 || static_cast<size_t>(n) >= cap - len) {
      ok = false;
      return;
    }
    len += static_cast<size_t>(n);
  }
};

}  // namespace

bool formatLinkRecord(const LinkRecord& r, const LinkRecordNames& names, const char* indent, char* out,
                      size_t cap) {
  if (cap == 0) return false;
  out[0] = '\0';
  Writer w{out, cap};
  auto ul = [](uint32_t v) { return static_cast<unsigned long>(v); };
  w.add("%sreset    %s; ended by: %s\n", indent, names.resetReason, restartCauseName(r.cause));
  w.add("%suptime   %lu s, %lu s since the last acked telemetry, %lu supervisor restart(s) in a row before\n",
        indent, ul(r.uptimeS), ul(r.sinceAckS), ul(r.restartChain));
  w.add("%swifi     %s, rssi %d dBm; %lu drop(s), last reason ", indent, names.wifiStatus,
        static_cast<int>(r.rssi), ul(r.wifiDrops));
  if (r.wifiReason == kNoReason) w.add("none");
  else w.add("%u %s", static_cast<unsigned>(r.wifiReason), names.wifiReason);
  w.add("; %lu retry(s), %lu forced reset(s)\n", ul(r.wifiRetries), ul(r.wifiResets));
  w.add("%smqtt     %lu connect(s), %lu drop(s), %lu ack timeout(s), last reason %s\n", indent,
        ul(r.mqttConnects), ul(r.mqttDrops), ul(r.ackTimeouts), mqttReasonName(r.mqttReason));
  w.add("%sbroker   name lookup %s; %lu failed lookup(s), %lu fallback(s) to the remembered address\n",
        indent, r.resolveFailing ? "FAILING" : "ok", ul(r.resolveFails), ul(r.fallbacks));
  return w.ok;
}

}  // namespace olvar
