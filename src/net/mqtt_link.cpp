#include "net/mqtt_link.h"

#include <ESPmDNS.h>
#include <WiFi.h>
#include <string.h>

#include "ids.h"
#include "link_record.h"
#include "telemetry.h"

namespace olvar {

namespace {

constexpr char kOnline[] = "online";
constexpr char kOffline[] = "offline";
constexpr char kLocalSuffix[] = ".local";

bool copy(char* out, size_t cap, const char* in) {
  if (in == nullptr) return false;
  return strlcpy(out, in, cap) < cap;
}

bool isLocalName(const char* host) {
  const size_t n = strlen(host);
  const size_t s = sizeof(kLocalSuffix) - 1;
  return n > s && strcasecmp(host + n - s, kLocalSuffix) == 0;
}

uint32_t rand01k() { return esp_random() % 1000; }

}  // namespace

MqttLink::MqttLink() : session_(millis()) {}

bool MqttLink::begin(const Config& cfg) {
  if (!copy(deviceId_, sizeof deviceId_, cfg.deviceId) ||
      !copy(password_, sizeof password_, cfg.password) || !copy(host_, sizeof host_, cfg.host) ||
      !copy(bootId_, sizeof bootId_, cfg.bootId) || !copy(fw_, sizeof fw_, cfg.fw) ||
      telemetryTopic(deviceId_, telemetryTopic_, sizeof telemetryTopic_) == 0 ||
      statusTopic(deviceId_, statusTopic_, sizeof statusTopic_) == 0) {
    return false;
  }
  port_ = cfg.port;
  session_ = MqttSession(millis());
  if (!parseIpv4(host_, hostIp_)) hostIp_ = 0;
  broker_ = BrokerAddress();
  broker_.remember(cfg.rememberedIp);
  onBrokerIp_ = cfg.onBrokerIp;

  client_.setClientId(deviceId_)
      .setCredentials(deviceId_, password_)
      .setCleanSession(true)
      .setKeepAlive(kKeepAliveS)
      .setWill(statusTopic_, 1, true, kOffline);
  // Runs in espMqttClient's own task: only record, loop() acts on it.
  client_.onPublish([this](uint16_t id) { acks_.record(id); });
  client_.onDisconnect([this](espMqttClientTypes::DisconnectReason reason) {
    disconnectReason_ = static_cast<uint8_t>(reason);
  });
  return true;
}

uint32_t MqttLink::resolve() {
  if (mdnsStale_ || broker_.mdnsRestartDue()) {
    // mDNS is restarted after a Wi-Fi reconnect and after repeated failed lookups, in case
    // the responder lost its socket with the old association.
    if (mdnsStarted_) MDNS.end();
    mdnsStarted_ = false;
    mdnsStale_ = false;
  }
  IPAddress ip;
  if (isLocalName(host_)) {
    // Resolved here with an explicit timeout. The query blocks loop() for up to
    // kMdnsTimeoutMs, once per attempt, which the backoff keeps rare.
    if (!mdnsStarted_) mdnsStarted_ = MDNS.begin(deviceId_);
    char name[sizeof host_];
    strlcpy(name, host_, strlen(host_) - (sizeof(kLocalSuffix) - 1) + 1);
    ip = MDNS.queryHost(name, kMdnsTimeoutMs);
  } else if (!WiFi.hostByName(host_, ip)) {
    ip = IPAddress();
  }
  // Only IPv4 is remembered; an IPv6-only answer counts as a failed lookup.
  if (ip.type() != IPv4) return 0;
  return static_cast<uint32_t>(ip);
}

bool MqttLink::brokerIp(IPAddress& out) {
  if (hostIp_ != 0) {
    out = IPAddress(hostIp_);
    return true;
  }
  const uint32_t ip = broker_.choose(resolve());
  if (ip == 0) {
    Serial.printf("[mqtt] cannot resolve %s and no address is remembered\n", host_);
    return false;
  }
  out = IPAddress(ip);
  if (broker_.source() == BrokerAddress::Source::Remembered) {
    Serial.printf("[mqtt] cannot resolve %s, trying the remembered %s\n", host_, out.toString().c_str());
  }
  return true;
}

bool MqttLink::startConnect() {
  // espMqttClient keeps unacked QoS 1 PUBLISH packets across a disconnect (_clearQueue(0))
  // and even after a clean-session CONNACK (_clearQueue(1)), so without this it would resend
  // old telemetry after a reconnect and the gateway would stamp it with the wrong time.
  client_.clearQueue(true);
  // Always dial an address: the client task would otherwise do its own DNS lookup, and a
  // failed lookup there looks like any other TCP failure, with no fallback.
  IPAddress ip;
  if (!brokerIp(ip)) return false;
  client_.setServer(ip, port_);
  Serial.printf("[mqtt] connecting to %s (%s):%u\n", host_, ip.toString().c_str(), port_);
  return client_.connect();
}

void MqttLink::loop(bool wifiUp) {
  const uint32_t now = millis();
  if (pending_.reconcile(acks_)) session_.acked(now);

  const uint32_t connects = session_.connects();
  const uint32_t ackTimeouts = session_.ackTimeouts();
  const MqttSession::Action action =
      session_.poll(now, wifiUp, client_.connected(), client_.disconnected(), rand01k());
  if (session_.connects() != connects && broker_.connected() && onBrokerIp_) {
    onBrokerIp_(broker_.known());
  }
  if (session_.ackTimeouts() != ackTimeouts) {
    Serial.printf("[mqtt] no PUBACK for %lu s, dropping the session\n",
                  static_cast<unsigned long>(MqttSession::kAckTimeoutMs / 1000));
  }
  switch (action) {
    case MqttSession::Action::None:
      break;
    case MqttSession::Action::Connect:
      if (!startConnect()) session_.connectFailed(now, rand01k());
      break;
    case MqttSession::Action::Discard: {
      const size_t dropped = client_.queueSize();
      if (!client_.disconnected()) client_.disconnect(true);
      client_.clearQueue(true);
      acks_.clear();
      pending_.clear();
      Serial.printf("[mqtt] disconnected: %s, discarded %u queued packet(s)\n",
                    mqttReasonName(disconnectReason_.load()), static_cast<unsigned>(dropped));
      break;
    }
    case MqttSession::Action::PublishOnline:
      if (client_.publish(statusTopic_, 1, true, kOnline) != 0) {
        session_.onlinePublished();
        Serial.printf("[mqtt] connected, %s = online\n", statusTopic_);
      }
      break;
    case MqttSession::Action::Abort:
      Serial.println("[mqtt] connect timed out");
      client_.disconnect(true);
      break;
  }
}

bool MqttLink::publishTelemetry(const Reading& r, uint32_t uptimeMs, OptI rssi) {
  // client_.connected() too: the library is built with EMC_ALLOW_NOT_CONNECTED_PUBLISH, so
  // it would accept and queue a publish while disconnected or still connecting.
  if (!session_.canPublish() || !client_.connected()) {
    session_.skipped();
    return false;
  }
  const Meta m{deviceId_, bootId_, session_.seq(), uptimeMs, fw_, rssi};
  const size_t n = buildTelemetryJson(r, m, payload_, sizeof payload_);
  const uint16_t id =
      n == 0 ? 0
             : client_.publish(telemetryTopic_, 1, false, reinterpret_cast<const uint8_t*>(payload_), n);
  if (id == 0) {
    session_.skipped();
    return false;
  }
  pending_.add(id);
  session_.handedOff(millis());
  return true;
}

void MqttLink::publishOfflineTask(void* arg) {
  auto* self = static_cast<MqttLink*>(arg);
  self->offlineId_ = self->client_.publish(self->statusTopic_, 1, true, kOffline);
  self->offlinePublished_ = true;
  vTaskDelete(nullptr);
}

void MqttLink::goOffline() {
  const uint32_t start = millis();
  // publish() waits forever for the client's semaphore, which its task holds through a
  // blocking socket write (up to ~10 s under TCP backpressure), so it runs in a helper task
  // and only the wait here is bounded. If the task cannot start, the will still says offline.
  offlinePublished_ = false;
  if (client_.connected() &&
      xTaskCreate(publishOfflineTask, "mqtt_offline", 3072, this, 1, nullptr) == pdPASS) {
    while (millis() - start < kGoOfflineMs) {
      if (offlinePublished_ && (offlineId_ == 0 || acks_.take(offlineId_))) break;
      delay(10);
    }
  }
  client_.disconnect(true);
  while (!client_.disconnected() && millis() - start < kGoOfflineMs) delay(10);
}

}  // namespace olvar
