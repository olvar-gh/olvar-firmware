#include "provisioning.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace olvar {

namespace {

const char kPasswordAlphabet[] = "abcdefghijkmnpqrstuvwxyz23456789";

void trimmed(const char* in, const char** begin, size_t* len) {
  if (!in) in = "";
  while (*in && isspace(static_cast<unsigned char>(*in))) ++in;
  size_t n = strlen(in);
  while (n && isspace(static_cast<unsigned char>(in[n - 1]))) --n;
  *begin = in;
  *len = n;
}

bool fail(const char** err, const char* msg) {
  if (err) *err = msg;
  return false;
}

bool hostCharsOk(const char* host) {
  for (; *host; ++host) {
    const char c = *host;
    if (!isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-') return false;
  }
  return true;
}

}  // namespace

void apSsid(const uint8_t mac[6], char out[kApSsidSize]) {
  snprintf(out, kApSsidSize, "olvar-setup-%02x%02x%02x", mac[3], mac[4], mac[5]);
}

void apPasswordFromRandom(uint32_t r0, uint32_t r1, char out[kApPasswordSize]) {
  uint64_t bits = (static_cast<uint64_t>(r1) << 32) | r0;
  for (size_t i = 0; i < kApPasswordSize - 1; ++i) {
    out[i] = kPasswordAlphabet[bits & 31];
    bits >>= 5;
  }
  out[kApPasswordSize - 1] = '\0';
}

bool shouldOpenPortal(bool hasWifiCredentials, bool portalFlag, bool settingsValid) {
  return !hasWifiCredentials || portalFlag || !settingsValid;
}

bool settingsFromForm(const PortalForm& form, const Settings& current, Settings& out, const char** err) {
  Settings s;
  const char* p;
  size_t n;

  trimmed(form.device_id, &p, &n);
  if (n >= sizeof(s.device_id)) {
    return fail(err, "device_id must be 3-32 chars of a-z, 0-9 or '-', starting with a letter or digit");
  }
  for (size_t i = 0; i < n; ++i) s.device_id[i] = static_cast<char>(tolower(static_cast<unsigned char>(p[i])));
  s.device_id[n] = '\0';

  trimmed(form.mqtt_host, &p, &n);
  if (n >= sizeof(s.mqtt_host)) return fail(err, "MQTT host must be at most 63 chars");
  memcpy(s.mqtt_host, p, n);
  s.mqtt_host[n] = '\0';
  if (!hostCharsOk(s.mqtt_host)) return fail(err, "MQTT host must be a host name or IP address");

  trimmed(form.mqtt_port, &p, &n);
  uint32_t port = 0;
  for (size_t i = 0; i < n; ++i) {
    if (!isdigit(static_cast<unsigned char>(p[i])) || port > 65535) return fail(err, "MQTT port must be 1-65535");
    port = port * 10 + static_cast<uint32_t>(p[i] - '0');
  }
  if (n == 0 || port == 0 || port > 65535) return fail(err, "MQTT port must be 1-65535");
  s.mqtt_port = static_cast<uint16_t>(port);

  const char* pass = form.mqtt_pass ? form.mqtt_pass : "";
  if (!pass[0]) pass = current.mqtt_pass;
  if (strnlen(pass, sizeof(s.mqtt_pass)) >= sizeof(s.mqtt_pass)) {
    return fail(err, "MQTT password must be at most 64 chars");
  }
  strcpy(s.mqtt_pass, pass);

  if (!validate(s, err)) return false;
  out = s;
  return true;
}

PortalStep portalStep(bool formValid, bool ssidSubmitted, bool joinedNewWifi, bool hasWifiCredentials) {
  if (!formValid) return joinedNewWifi ? PortalStep::Reopen : PortalStep::Wait;
  if (ssidSubmitted) return joinedNewWifi ? PortalStep::Save : PortalStep::WifiFailed;
  return hasWifiCredentials ? PortalStep::Save : PortalStep::NeedWifi;
}

bool portalExpired(uint32_t lastActivityMs, uint32_t nowMs) {
  return nowMs - lastActivityMs >= kPortalTimeoutMs;
}

bool bootNormallyAfterTimeout(bool hasWifiCredentials, const Settings& current) {
  return hasWifiCredentials && validate(current, nullptr);
}

bool LongPressDetector::update(bool pressed, uint32_t nowMs) {
  if (!started_ || pressed != raw_) {
    started_ = true;
    raw_ = pressed;
    rawSinceMs_ = nowMs;
  }
  if (nowMs - rawSinceMs_ < kButtonDebounceMs) return false;
  if (raw_) {
    if (!stable_) {
      stable_ = true;
      pressedAtMs_ = rawSinceMs_;
    }
    return false;
  }
  // A press already in progress at the first poll has an unknown start, so it never counts.
  const bool fired = stable_ && armed_ && rawSinceMs_ - pressedAtMs_ >= kLongPressMs;
  stable_ = false;
  armed_ = true;
  return fired;
}

}  // namespace olvar

#ifdef ARDUINO
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <esp_wifi.h>

#include "pins.h"

namespace olvar {

bool hasWifiCredentials() {
  if (WiFi.getMode() == WIFI_MODE_NULL) WiFi.mode(WIFI_STA);
  wifi_config_t conf = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK) return false;
  return conf.sta.ssid[0] != 0;
}

void BootButton::begin() { pinMode(pins::kBoot, INPUT_PULLUP); }

bool BootButton::poll() { return detector_.update(digitalRead(pins::kBoot) == LOW, millis()); }

bool Provisioning::needed(const Settings& current) {
  return shouldOpenPortal(hasWifiCredentials(), store_.portalRequested(), validate(current, nullptr));
}

void Provisioning::onParamsSaved() {
  const PortalForm form{devId_->getValue(), host_->getValue(), port_->getValue(), pass_->getValue()};
  const char* err = nullptr;
  formValid_ = settingsFromForm(form, current_, pending_, &err);
  // WiFiManager also joins when only a password is posted, reusing the saved network name.
  ssidSubmitted_ = wm_.server && (wm_.server->arg("s").length() > 0 || wm_.server->arg("p").length() > 0);
  if (formValid_) {
    banner_[0] = '\0';
  } else {
    snprintf(banner_, sizeof(banner_), "<p style='color:#c00'>Not saved: %s.</p>", err);
    Serial.printf("[portal] form rejected: %s\n", err);
  }
}

void Provisioning::run(const Settings& current) {
  current_ = current;
  // Keep the flag set while the portal is open, so a power cut mid-setup reopens it.
  store_.setPortalRequested(true);

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char ssid[kApSsidSize];
  char pass[kApPasswordSize];
  apSsid(mac, ssid);
  apPasswordFromRandom(esp_random(), esp_random(), pass);
  Serial.printf("[portal] device_id %s\n", current.device_id);
  Serial.printf("[portal] join Wi-Fi \"%s\" with password \"%s\", then open http://192.168.4.1\n", ssid, pass);

  snprintf(portText_, sizeof(portText_), "%u", current.mqtt_port);
  static WiFiManagerParameter banner(banner_);
  // WiFiManager silently truncates submitted values to these lengths, which match the Settings limits.
  devId_ = new WiFiManagerParameter("dev_id", "Device id (MQTT username)", current.device_id, 32);
  host_ = new WiFiManagerParameter("mq_host", "MQTT host (gateway IP or name)", current.mqtt_host, 63);
  port_ = new WiFiManagerParameter("mq_port", "MQTT port", portText_, 5, "inputmode='numeric'");
  pass_ = new WiFiManagerParameter("mq_pass", "MQTT password", "", 64,
                                   current.mqtt_pass[0] ? "type='password' placeholder='(unchanged)'"
                                                        : "type='password'");
  wm_.addParameter(&banner);
  wm_.addParameter(devId_);
  wm_.addParameter(host_);
  wm_.addParameter(port_);
  wm_.addParameter(pass_);
  wm_.setSaveParamsCallback([this] { onParamsSaved(); });
  wm_.setTitle("Olvar setup");
  wm_.setShowInfoUpdate(false);
  wm_.setConnectTimeout(20);
  wm_.setConfigPortalBlocking(false);
  wm_.startConfigPortal(ssid, pass);

  uint32_t lastActivity = millis();
  for (;;) {
    const bool joined = wm_.process();
    const uint32_t now = millis();
    if (WiFi.softAPgetStationNum() > 0) lastActivity = now;

    switch (portalStep(formValid_, ssidSubmitted_, joined, formValid_ && hasWifiCredentials())) {
      case PortalStep::Save: {
        const char* err = nullptr;
        if (store_.save(pending_, &err)) {
          store_.setPortalRequested(false);
          Serial.printf("[portal] saved; restarting as %s\n", pending_.device_id);
          delay(500);
          ESP.restart();
        }
        snprintf(banner_, sizeof(banner_), "<p style='color:#c00'>Not saved: %s.</p>", err ? err : "unknown error");
        formValid_ = false;
        break;
      }
      case PortalStep::WifiFailed:
        snprintf(banner_, sizeof(banner_), "<p style='color:#c00'>Could not join that Wi-Fi network. "
                                           "Check its name and password.</p>");
        formValid_ = false;
        break;
      case PortalStep::NeedWifi:
        snprintf(banner_, sizeof(banner_), "<p style='color:#c00'>Choose a Wi-Fi network.</p>");
        formValid_ = false;
        break;
      case PortalStep::Reopen:
        Serial.println("[portal] Wi-Fi saved but the MQTT fields were not; reopening the portal");
        delay(500);
        ESP.restart();
      case PortalStep::Wait:
        break;
    }

    if (portalExpired(lastActivity, now)) {
      const bool normal = bootNormallyAfterTimeout(hasWifiCredentials(), current_);
      store_.setPortalRequested(!normal);
      Serial.printf("[portal] timed out; restarting into %s\n", normal ? "normal mode" : "the portal");
      delay(500);
      ESP.restart();
    }
    delay(5);
  }
}

}  // namespace olvar
#endif  // ARDUINO
