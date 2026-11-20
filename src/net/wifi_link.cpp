#include "net/wifi_link.h"

namespace olvar {

void WifiLink::begin(const char* hostname) {
  // The hostname is applied when the STA interface starts, so it must be set before mode().
  WiFi.setHostname(hostname);
  // USB powered, so no modem sleep: with it the radio only wakes for DTIM beacons, and
  // multicast (mDNS answers) and the broker's packets wait for those or get lost.
  WiFi.setSleep(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.onEvent([this](arduino_event_id_t e, arduino_event_info_t i) { onEvent(e, i); });
  WiFi.begin();
  retry_ = WifiRetry(millis());
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
    ++ups_;
    Serial.printf("[wifi] connected, ip %s, rssi %ld dBm\n", ip().toString().c_str(),
                  static_cast<long>(rssi()));
  }
  wasConnected_ = up;
  if (retry_.poll(millis(), up)) {
    ++retries_;
    Serial.printf("[wifi] down for %lu s (status %d), reconnecting\n",
                  static_cast<unsigned long>(WifiRetry::kDownMs / 1000), static_cast<int>(WiFi.status()));
    reconnect();
  }
}

void WifiLink::reconnect() {
  // reconnect() fails when the station is not started (it was stopped); begin() starts it and
  // connects with the stored credentials.
  if (!WiFi.reconnect()) WiFi.begin();
}

void WifiLink::reset() {
  ++resets_;
  // reconnect() disconnects first when associated. That disconnect reports ASSOC_LEAVE, which
  // the core does not auto-reconnect on, but reconnect() has already asked to connect.
  reconnect();
  retry_ = WifiRetry(millis());
}

}  // namespace olvar
