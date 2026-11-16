// MQTT link to the gateway broker (docs/PLAN.md §4.1, T6).
//
// MqttSession is the connection policy with no Arduino dependency, so the native tests can
// include this header (they build without ARDUINO). MqttLink binds it to espMqttClient.
#pragma once

#include <stdint.h>

#include "backoff.h"

namespace olvar {

class MqttSession {
 public:
  enum class State : uint8_t { Waiting, Connecting, Connected };
  enum class Action : uint8_t { None, Connect, Discard, PublishOnline, Abort, Restart };

  static constexpr uint32_t kBackoffMinMs = 1000;
  static constexpr uint32_t kBackoffMaxMs = 60000;
  static constexpr uint32_t kConnectTimeoutMs = 30000;
  static constexpr uint32_t kSupervisorMs = 10UL * 60UL * 1000UL;

  explicit MqttSession(uint32_t nowMs) : backoff_(kBackoffMinMs, kBackoffMaxMs), lastAck_(nowMs), nextTry_(nowMs) {}

  // Called from loop() only. `connected`/`disconnected` are the client's own states; both
  // false means it is still connecting or disconnecting. rand01k feeds the backoff jitter.
  Action poll(uint32_t nowMs, bool wifiUp, bool connected, bool disconnected, uint32_t rand01k) {
    if (static_cast<uint32_t>(nowMs - lastAck_) >= kSupervisorMs) return Action::Restart;
    switch (state_) {
      case State::Connected:
        if (connected) {
          if (onlinePending_) return Action::PublishOnline;
          return Action::None;
        }
        return lose(nowMs, rand01k);
      case State::Connecting:
        if (connected) {
          state_ = State::Connected;
          backoff_.reset();
          onlinePending_ = true;
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
  void handedOff() { ++seq_; }
  void skipped() { ++skipped_; }
  void acked(uint32_t nowMs) { lastAck_ = nowMs; }

  State state() const { return state_; }
  uint32_t skippedCount() const { return skipped_; }
  uint32_t msSinceAck(uint32_t nowMs) const { return nowMs - lastAck_; }

 private:
  Action lose(uint32_t nowMs, uint32_t rand01k) {
    state_ = State::Waiting;
    onlinePending_ = false;
    nextTry_ = nowMs + backoff_.next(rand01k);
    return Action::Discard;
  }

  Backoff backoff_;
  State state_ = State::Waiting;
  bool onlinePending_ = false;
  bool abortSent_ = false;
  uint32_t seq_ = 0;
  uint32_t skipped_ = 0;
  uint32_t lastAck_;
  uint32_t nextTry_;
  uint32_t connectStart_ = 0;
};

}  // namespace olvar

#ifdef ARDUINO

#include <Arduino.h>
#include <espMqttClient.h>

#include <atomic>

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
  };

  static constexpr uint16_t kKeepAliveS = 30;
  static constexpr uint32_t kGoOfflineMs = 500;
  static constexpr uint32_t kMdnsTimeoutMs = 1000;

  MqttLink();

  // Copies every string, so cfg may be temporary.
  bool begin(const Config& cfg);
  // Drives reconnects, the retained `online` status and the no-ack supervisor (restarts the
  // board after kSupervisorMs without an acknowledged telemetry message).
  void loop(bool wifiUp);

  // QoS 1, not retained. Returns false (and counts the interval as skipped, without using a
  // seq) when not connected; nothing is ever queued while disconnected.
  bool publishTelemetry(const Reading& r, uint32_t uptimeMs, OptI rssi);
  // Publishes retained `offline`, waits up to kGoOfflineMs for its ack, then disconnects.
  void goOffline();

  bool connected() const { return session_.canPublish(); }
  MqttSession::State state() const { return session_.state(); }
  uint32_t seq() const { return session_.seq(); }
  uint32_t skipped() const { return session_.skippedCount(); }

 private:
  bool startConnect();

  espMqttClient client_;
  MqttSession session_;
  char deviceId_[33] = {};
  char password_[65] = {};
  char host_[64] = {};
  uint16_t port_ = 1883;
  char bootId_[9] = {};
  char fw_[32] = {};
  char telemetryTopic_[64] = {};
  char statusTopic_[64] = {};
  char payload_[320] = {};
  bool mdnsStarted_ = false;
  std::atomic<uint16_t> telemetryId_{0};
  std::atomic<uint16_t> offlineId_{0};
  std::atomic<bool> telemetryAcked_{false};
  std::atomic<bool> offlineAcked_{false};
  std::atomic<uint8_t> disconnectReason_{0};
};

}  // namespace olvar

#endif  // ARDUINO
