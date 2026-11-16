#include "net/wifi_link.h"

namespace olvar {

void WifiLink::begin(const char* hostname) {
  // The hostname is applied when the STA interface starts, so it must be set before mode().
  WiFi.setHostname(hostname);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.onEvent([this](arduino_event_id_t e, arduino_event_info_t i) { onEvent(e, i); });
  WiFi.begin();
}

void WifiLink::onEvent(arduino_event_id_t event, arduino_event_info_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    lastReason_ = info.wifi_sta_disconnected.reason;
    disconnects_.fetch_add(1);
  }
}

void WifiLink::loop() {
  const uint32_t n = disconnects_;
  if (n != logged_) {
    const uint8_t reason = lastReason_;
    Serial.printf("[wifi] disconnected (%lu): reason %u %s\n", static_cast<unsigned long>(n),
                  reason, WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(reason)));
    logged_ = n;
  }
  const bool up = connected();
  if (up && !wasConnected_) {
    Serial.printf("[wifi] connected, ip %s, rssi %ld dBm\n", ip().toString().c_str(),
                  static_cast<long>(rssi()));
  }
  wasConnected_ = up;
}

}  // namespace olvar
