// MQTT link to the gateway broker (docs/PLAN.md §4.1, T6).
//
// MqttSession (connection policy) and AckLog have no Arduino dependency, so the native tests
// can include this header (they build without ARDUINO). MqttLink binds them to espMqttClient.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <atomic>

#include "backoff.h"

namespace olvar {

class MqttSession {
 public:
  enum class State : uint8_t { Waiting, Connecting, Connected };
  enum class Action : uint8_t { None, Connect, Discard, PublishOnline, Abort };

  // Short enough that a broker that is back is found again within about 20 s.
  static constexpr uint32_t kBackoffMinMs = 1000;
  static constexpr uint32_t kBackoffMaxMs = 15000;
  static constexpr uint32_t kConnectTimeoutMs = 30000;
  // The broker acks a QoS 1 message within milliseconds on a LAN, and the client resends it
  // after 10 s. A message still unacked after this long means the session is dead even if the
  // client still reports it connected, so it is dropped and a new one is opened.
  static constexpr uint32_t kAckTimeoutMs = 30000;

  explicit MqttSession(uint32_t nowMs) : backoff_(kBackoffMinMs, kBackoffMaxMs), lastAck_(nowMs), nextTry_(nowMs) {}

  // Called from loop() only. `connected`/`disconnected` are the client's own states; both
  // false means it is still connecting or disconnecting. rand01k feeds the backoff jitter.
  // Discard means: drop the outbox and close the connection if the client still holds one.
  Action poll(uint32_t nowMs, bool wifiUp, bool connected, bool disconnected, uint32_t rand01k) {
    // The client can report a cached `connected` until its keepalive notices the loss; a
    // session that survived the outage would then deliver readings taken during it, late.
    if (!wifiUp && state_ != State::Waiting) return lose(nowMs, rand01k);
    switch (state_) {
      case State::Connected:
        if (!connected) return lose(nowMs, rand01k);
        if (awaiting_ && static_cast<uint32_t>(nowMs - awaitingSince_) >= kAckTimeoutMs) {
          ++ackTimeouts_;
          return lose(nowMs, rand01k);
        }
        if (onlinePending_) return Action::PublishOnline;
        return Action::None;
      case State::Connecting:
        if (connected) {
          state_ = State::Connected;
          backoff_.reset();
          onlinePending_ = true;
          ++connects_;
          return Action::PublishOnline;
        }
        if (disconnected) return lose(nowMs, rand01k);
        if (!abortSent_ && static_cast<uint32_t>(nowMs - connectStart_) >= kConnectTimeoutMs) {
          abortSent_ = true;
          return Action::Abort;
        }
        return Action::None;
      case State::Waiting:
        if (!wifiUp || !disconnected) return Action::None;
        if (static_cast<int32_t>(nowMs - nextTry_) < 0) return Action::None;
        state_ = State::Connecting;
        connectStart_ = nowMs;
        abortSent_ = false;
        return Action::Connect;
    }
    return Action::None;
  }

  // The client refused to start a connection (e.g. the broker name did not resolve).
  void connectFailed(uint32_t nowMs, uint32_t rand01k) {
    state_ = State::Waiting;
    nextTry_ = nowMs + backoff_.next(rand01k);
  }

  void onlinePublished() { onlinePending_ = false; }

  bool canPublish() const { return state_ == State::Connected; }
  uint32_t seq() const { return seq_; }
  // The message carrying seq() is in the client's hands: the broker may receive it even if
  // it is discarded later, so its number is never handed out again.
  void handedOff(uint32_t nowMs) {
    ++seq_;
    if (!awaiting_) {
      awaiting_ = true;
      awaitingSince_ = nowMs;
    }
  }
  void skipped() { ++skipped_; }
  // Any telemetry ack: the broker is reading what this session sends.
  void acked(uint32_t nowMs) {
    lastAck_ = nowMs;
    awaiting_ = false;
  }

  State state() const { return state_; }
  uint32_t skippedCount() const { return skipped_; }
  uint32_t msSinceAck(uint32_t nowMs) const { return nowMs - lastAck_; }
  uint32_t connects() const { return connects_; }
  // Sessions that were up and then lost, for whatever reason.
  uint32_t drops() const { return drops_; }
  uint32_t ackTimeouts() const { return ackTimeouts_; }

 private:
  Action lose(uint32_t nowMs, uint32_t rand01k) {
    if (state_ == State::Connected) ++drops_;
    state_ = State::Waiting;
    onlinePending_ = false;
    awaiting_ = false;
    nextTry_ = nowMs + backoff_.next(rand01k);
    return Action::Discard;
  }

  Backoff backoff_;
  State state_ = State::Waiting;
  bool onlinePending_ = false;
  bool abortSent_ = false;
  bool awaiting_ = false;
  uint32_t seq_ = 0;
  uint32_t skipped_ = 0;
  uint32_t lastAck_;
  uint32_t nextTry_;
  uint32_t connectStart_ = 0;
  uint32_t awaitingSince_ = 0;
  uint32_t connects_ = 0;
  uint32_t drops_ = 0;
  uint32_t ackTimeouts_ = 0;
};

// PUBACK ids recorded by espMqttClient's task and matched on the loop thread. The ack can
// arrive before publish() has returned the id to the caller, so it must be kept until then.
class AckLog {
 public:
  static constexpr uint8_t kSlots = 8;

  void record(uint16_t id) {
    if (id == 0) return;
    ids_[next_.fetch_add(1) % kSlots].store(id);
  }

  // True once per recorded id.
  bool take(uint16_t id) {
    if (id == 0) return false;
    for (auto& slot : ids_) {
      uint16_t expected = id;
      if (slot.compare_exchange_strong(expected, 0)) return true;
    }
    return false;
  }

  void clear() {
    for (auto& slot : ids_) slot.store(0);
  }

 private:
  std::atomic<uint16_t> ids_[kSlots] = {};
  std::atomic<uint8_t> next_{0};
};

// Telemetry packet ids still waiting for a PUBACK, owned by the loop thread. Several can be
// outstanding when acks are slower than the publish interval; the oldest is dropped when full.
class PendingAcks {
 public:
  static constexpr uint8_t kSlots = 8;

  void add(uint16_t id) {
    if (id == 0) return;
    ids_[next_] = id;
    next_ = static_cast<uint8_t>((next_ + 1) % kSlots);
  }

  // True if the log holds an ack for any pending id; those ids stop being pending.
  bool reconcile(AckLog& log) {
    bool any = false;
    for (uint16_t& id : ids_) {
      if (id != 0 && log.take(id)) {
        id = 0;
        any = true;
      }
    }
    return any;
  }

  size_t size() const {
    size_t n = 0;
    for (uint16_t id : ids_) n += id != 0;
    return n;
  }

  void clear() {
    for (uint16_t& id : ids_) id = 0;
  }

 private:
  uint16_t ids_[kSlots] = {};
  uint8_t next_ = 0;
};

}  // namespace olvar

#ifdef ARDUINO

#include <Arduino.h>
#include <espMqttClient.h>

#include <functional>

#include "link_recovery.h"
#include "reading.h"

namespace olvar {

class MqttLink {
 public:
  struct Config {
    const char* deviceId;
    const char* password;
    const char* host;
    uint16_t port;
    const char* bootId;
    const char* fw;
    // The last address the broker name resolved to and that accepted a connection (0 if
    // none), used when the name does not resolve. Ignored when host is an IP address.
    uint32_t rememberedIp;
    // Called on the loop thread when a newly resolved address accepted a connection.
    std::function<void(uint32_t ip)> onBrokerIp;
  };

  // The client pings after kKeepAliveS without traffic and drops a silent server after twice
  // that, so a half-open TCP connection is noticed within 30 s.
  static constexpr uint16_t kKeepAliveS = 15;
  static constexpr uint32_t kGoOfflineMs = 500;
  static constexpr uint32_t kMdnsTimeoutMs = 1000;

  MqttLink();

  // Copies every string, so cfg may be temporary.
  bool begin(const Config& cfg);
  // Drives reconnects and the retained `online` status. Escalation beyond a reconnect (Wi-Fi
  // reset, restart) is the caller's, from msSinceAck().
  void loop(bool wifiUp);
  // Wi-Fi came back or was reset: the mDNS responder is restarted before the next lookup.
  void networkChanged() { mdnsStale_ = true; }

  // QoS 1, not retained. Returns false (and counts the interval as skipped, without using a
  // seq) when not connected; nothing is ever queued while disconnected.
  bool publishTelemetry(const Reading& r, uint32_t uptimeMs, OptI rssi);
  // Publishes retained `offline`, waits for its ack, then disconnects, all within
  // kGoOfflineMs. Only for use right before a restart: on timeout a helper task may still be
  // blocked inside the client.
  void goOffline();

  bool connected() const { return session_.canPublish(); }
  MqttSession::State state() const { return session_.state(); }
  uint32_t seq() const { return session_.seq(); }
  uint32_t skipped() const { return session_.skippedCount(); }
  uint32_t msSinceAck(uint32_t nowMs) const { return session_.msSinceAck(nowMs); }
  uint32_t connects() const { return session_.connects(); }
  uint32_t drops() const { return session_.drops(); }
  uint32_t ackTimeouts() const { return session_.ackTimeouts(); }
  // espMqttClientTypes::DisconnectReason of the last disconnect, kNoReason before the first.
  uint8_t lastDisconnectReason() const { return disconnectReason_.load(); }
  const BrokerAddress& broker() const { return broker_; }

 private:
  bool startConnect();
  // The broker's address for this attempt, resolved or remembered; false if there is none.
  bool brokerIp(IPAddress& out);
  uint32_t resolve();

  espMqttClient client_;
  MqttSession session_;
  BrokerAddress broker_;
  std::function<void(uint32_t)> onBrokerIp_;
  char deviceId_[33] = {};
  char password_[65] = {};
  char host_[64] = {};
  uint32_t hostIp_ = 0;  // mq_host as an address, 0 when it is a name
  uint16_t port_ = 1883;
  char bootId_[9] = {};
  char fw_[32] = {};
  char telemetryTopic_[64] = {};
  char statusTopic_[64] = {};
  char payload_[320] = {};
  bool mdnsStarted_ = false;
  bool mdnsStale_ = false;
  static void publishOfflineTask(void* arg);

  AckLog acks_;
  PendingAcks pending_;
  std::atomic<uint16_t> offlineId_{0};
  std::atomic<bool> offlinePublished_{false};
  std::atomic<uint8_t> disconnectReason_{0xFF};
};

}  // namespace olvar

#endif  // ARDUINO
