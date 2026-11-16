// Wi-Fi station link. Credentials come from the Wi-Fi driver's NVS store (written by the
// WiFiManager portal, T5), so begin() takes only the hostname.
#pragma once

#include <Arduino.h>
#include <WiFi.h>

#include <atomic>

namespace olvar {

class WifiLink {
 public:
  // hostname must outlive the link (it is the device_id).
  void begin(const char* hostname);
  // Logs disconnect reasons recorded by the event handler, which runs in another task.
  void loop();

  bool connected() const { return WiFi.status() == WL_CONNECTED; }
  int32_t rssi() const { return connected() ? WiFi.RSSI() : 0; }
  IPAddress ip() const { return WiFi.localIP(); }
  uint8_t lastDisconnectReason() const { return lastReason_.load(); }
  uint32_t disconnects() const { return disconnects_.load(); }

 private:
  void onEvent(arduino_event_id_t event, arduino_event_info_t info);

  std::atomic<uint8_t> lastReason_{0};
  std::atomic<uint32_t> disconnects_{0};
  uint32_t logged_ = 0;
  bool wasConnected_ = false;
};

}  // namespace olvar
