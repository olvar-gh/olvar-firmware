// Link recovery policy: when to nudge Wi-Fi, when to escalate, which broker address to dial.
// Pure C++17, no Arduino.h, so it builds in native tests. All times are millis() values and
// every comparison is wraparound-safe.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace olvar {

// The Arduino core only re-associates after some disconnect reasons (AUTH_FAIL after the first
// connect, ASSOC_LEAVE and any reason missing from its list leave the station idle for good).
// WifiRetry asks for a reconnect once Wi-Fi has been down for kDownMs, then every kDownMs.
class WifiRetry {
 public:
  static constexpr uint32_t kDownMs = 30000;

  explicit WifiRetry(uint32_t nowMs) : since_(nowMs) {}

  // True when the caller should reconnect now.
  bool poll(uint32_t nowMs, bool connected) {
    if (connected) {
      since_ = nowMs;
      return false;
    }
    if (static_cast<uint32_t>(nowMs - since_) < kDownMs) return false;
    since_ = nowMs;
    return true;
  }

 private:
  uint32_t since_;
};

// Staged escalation keyed on the time since the last acknowledged telemetry message. The MQTT
// stage (dropping a connection whose acks stopped) lives in MqttSession; this is what follows:
// a full Wi-Fi reconnect after kWifiResetMs and again every kWifiResetMs, and a restart after
// kRestartMs as the last resort.
class LinkRecovery {
 public:
  enum class Action : uint8_t { None, ResetWifi, Restart };

  static constexpr uint32_t kWifiResetMs = 100000;
  static constexpr uint32_t kRestartMs = 10UL * 60UL * 1000UL;

  Action poll(uint32_t nowMs, uint32_t msSinceAck) {
    if (msSinceAck >= kRestartMs) return Action::Restart;
    if (msSinceAck < kWifiResetMs) {
      resetPending_ = false;
      return Action::None;
    }
    if (resetPending_ && static_cast<uint32_t>(nowMs - lastReset_) < kWifiResetMs) return Action::None;
    resetPending_ = true;
    lastReset_ = nowMs;
    return Action::ResetWifi;
  }

 private:
  bool resetPending_ = false;
  uint32_t lastReset_ = 0;
};

// IPv4 addresses are uint32_t in network byte order, the layout of lwIP's ip4_addr_t and of
// Arduino's IPAddress(uint32_t): the first octet is the lowest byte. 0 means "none".
uint32_t ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d);
// Strict dotted quad ("192.168.0.42"); false for anything else, including names.
bool parseIpv4(const char* s, uint32_t& out);
// Writes "a.b.c.d" (needs 16 bytes); returns false if cap is too small.
bool formatIpv4(uint32_t ip, char* out, size_t cap);

// The NVS memo of the last broker address that accepted a connection: "<ip> <host>". It only
// counts for the host it was learnt for, so a new mq_host from the portal never reuses it.
constexpr size_t kBrokerMemoLen = 16 + 64;
bool formatBrokerMemo(uint32_t ip, const char* host, char* out, size_t cap);
uint32_t parseBrokerMemo(const char* memo, const char* host);

// Chooses the address for each MQTT connect attempt when mq_host is a name.
class BrokerAddress {
 public:
  enum class Source : uint8_t { None, Resolved, Remembered };

  // After this many failed lookups in a row the mDNS responder is restarted.
  static constexpr uint8_t kMdnsRestartAfter = 2;

  // ip: the remembered address (from NVS), 0 if none.
  void remember(uint32_t ip) { known_ = ip; }
  uint32_t known() const { return known_; }

  // resolved: the lookup result, 0 when the name did not resolve. Returns the address to dial,
  // 0 if there is none; source() says where it came from.
  uint32_t choose(uint32_t resolved) {
    if (resolved != 0) {
      failStreak_ = 0;
      source_ = Source::Resolved;
      candidate_ = resolved;
      return resolved;
    }
    if (failStreak_ < UINT8_MAX) ++failStreak_;
    ++failures_;
    candidate_ = known_;
    source_ = known_ != 0 ? Source::Remembered : Source::None;
    if (known_ != 0) ++fallbacks_;
    return known_;
  }

  // The broker at the chosen address accepted the connection. True when that address is new
  // and should be written to NVS.
  bool connected() {
    if (candidate_ == 0 || candidate_ == known_) return false;
    known_ = candidate_;
    return true;
  }

  // True once every kMdnsRestartAfter failed lookups in a row.
  bool mdnsRestartDue() const { return failStreak_ != 0 && failStreak_ % kMdnsRestartAfter == 0; }
  bool failing() const { return failStreak_ != 0; }
  Source source() const { return source_; }
  uint32_t failures() const { return failures_; }
  uint32_t fallbacks() const { return fallbacks_; }

 private:
  uint32_t known_ = 0;
  uint32_t candidate_ = 0;
  uint8_t failStreak_ = 0;
  Source source_ = Source::None;
  uint32_t failures_ = 0;
  uint32_t fallbacks_ = 0;
};

}  // namespace olvar
