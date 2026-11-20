// Wi-Fi station link. Credentials come from the Wi-Fi driver's NVS store (written by the
// WiFiManager portal, T5), so begin() takes only the hostname.
#pragma once

#include <Arduino.h>
#include <WiFi.h>

#include <atomic>

#include "link_recovery.h"

namespace olvar {

class WifiLink {
 public:
  // hostname must outlive the link (it is the device_id).
  void begin(const char* hostname);
  // Logs disconnect reasons recorded by the event handler, which runs in another task, and
  // reconnects when the core's auto-reconnect has given up (see WifiRetry).
  void loop();
  // Recovery ladder stage: drop the association and connect again (new DHCP lease too).
  void reset();

  bool connected() const { return WiFi.status() == WL_CONNECTED; }
  int32_t rssi() const { return connected() ? WiFi.RSSI() : 0; }
  IPAddress ip() const { return WiFi.localIP(); }
  uint8_t lastDisconnectReason() const { return lastReason_.load(); }
  uint32_t disconnects() const { return disconnects_.load(); }
  uint32_t retries() const { return retries_; }
  uint32_t resets() const { return resets_; }
  // Times the link came up, the first connection included.
  uint32_t ups() const { return ups_; }

 private:
  void onEvent(arduino_event_id_t event, arduino_event_info_t info);
  void reconnect();

  std::atomic<uint8_t> lastReason_{0xFF};
  std::atomic<uint32_t> disconnects_{0};
  uint32_t logged_ = 0;
  bool wasConnected_ = false;
  WifiRetry retry_{0};
  uint32_t retries_ = 0;
  uint32_t resets_ = 0;
  uint32_t ups_ = 0;
};

}  // namespace olvar
