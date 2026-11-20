#include "app.h"

#include <esp_attr.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <esp_system.h>

#include "ids.h"
#include "pins.h"

namespace olvar {

namespace {

constexpr uint32_t kIntervalMs = OLVAR_INTERVAL_S * 1000UL;
constexpr uint32_t kRecordEveryMs = 1000;

// The ladder counts from the last acked telemetry, so it assumes a reading every interval.
static_assert(kIntervalMs + MqttSession::kAckTimeoutMs < LinkRecovery::kWifiResetMs,
              "the Wi-Fi reset would fire between two healthy readings");

// Kept by a software restart, panic or watchdog reset; random after power-up (the checksum
// tells). Written by this boot, read by the next.
RTC_NOINIT_ATTR LinkRecord rtcRecord;

const char* resetReasonName(uint8_t r) {
  switch (static_cast<esp_reset_reason_t>(r)) {
    case ESP_RST_POWERON: return "POWERON (power-up)";
    case ESP_RST_EXT: return "EXT (reset pin)";
    case ESP_RST_SW: return "SW (software restart)";
    case ESP_RST_PANIC: return "PANIC (crash)";
    case ESP_RST_INT_WDT: return "INT_WDT (interrupt watchdog)";
    case ESP_RST_TASK_WDT: return "TASK_WDT (task watchdog)";
    case ESP_RST_WDT: return "WDT (other watchdog)";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT (supply dipped)";
    case ESP_RST_SDIO: return "SDIO";
    case ESP_RST_USB: return "USB";
    case ESP_RST_JTAG: return "JTAG";
    case ESP_RST_EFUSE: return "EFUSE";
    case ESP_RST_PWR_GLITCH: return "PWR_GLITCH (power glitch)";
    case ESP_RST_CPU_LOCKUP: return "CPU_LOCKUP";
    default: return "UNKNOWN";
  }
}

const char* wifiStatusName(uint8_t s) {
  switch (s) {
    case WL_IDLE_STATUS: return "IDLE (associated, no IP yet)";
    case WL_NO_SSID_AVAIL: return "NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED: return "SCAN_COMPLETED";
    case WL_CONNECTED: return "CONNECTED";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
    case WL_STOPPED: return "STOPPED";
    case WL_NO_SHIELD: return "NO_SHIELD (Wi-Fi off)";
    default: return "?";
  }
}

}  // namespace

App::App()
    : climate_(bus_),
      bh_(bus_),
      store_(kv_),
      led_(pins::kLed),
      console_(Serial, climate_, bh_, soil_,
               CliHooks{[this](CliStatus& s) { fillStatus(s); },
                        [this] { store_.setPortalRequested(true); },
                        [this] { restart(store_.portalRequested() ? RestartCause::Portal : RestartCause::Command); }}),
      schedule_(kIntervalMs, 0) {}

void App::setup() {
  Serial.begin(SerialCli::kBaud);
  Serial.printf("\nolvar-firmware %s\n", OLVAR_FW_VERSION);
  led_.begin();

  prevRecordValid_ = linkRecordValid(rtcRecord);
  if (prevRecordValid_) prevRecord_ = rtcRecord;
  rtcRecord = newBootRecord(prevRecordValid_ ? &prevRecord_ : nullptr, esp_reset_reason());
  Serial.printf("reset reason %s\n", resetReasonName(rtcRecord.resetReason));
  if (prevRecordValid_) {
    Serial.println("previous boot (see BRINGUP.md, 'Reading the link record'):");
    printLinkRecord(prevRecord_, "  ", linkText_, sizeof linkText_);
    Serial.print(linkText_);
  }

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  store_.load(settings_, defaultSettings(mac));
  Serial.printf("device_id %s\n", settings_.device_id);

  // Provisioning's credential check starts the STA interface, which only picks up a hostname
  // set before it starts.
  WiFi.setHostname(settings_.device_id);
  {
    auto* portal = new Provisioning(store_);
    if (portal->needed(settings_)) portal->run(settings_);
    delete portal;
  }

  bus_.begin();
  if (climate_.begin()) {
    Serial.printf("[sensors] temperature/humidity: %s\n", climate_.name());
  } else {
    Serial.println("[sensors] no SHT31 (0x44/0x45) or DHT11 (GPIO4) found; temperature/humidity "
                   "will be null until one answers");
  }
  if (!bh_.begin()) Serial.println("[sensors] BH1750 not found at 0x23; lux will be null");
  soil_.begin();
  button_.begin();

  wifi_.begin(settings_.device_id);
  // esp_random() only draws on hardware entropy once the radio is running.
  bootIdFromRandom(esp_random(), bootId_);
  Serial.printf("boot_id %s\n", bootId_);

  const uint32_t remembered = store_.brokerIp(settings_.mqtt_host);
  const MqttLink::Config cfg{settings_.device_id, settings_.mqtt_pass, settings_.mqtt_host,
                             settings_.mqtt_port, bootId_, OLVAR_FW_VERSION, remembered,
                             [this](uint32_t ip) {
                               char a[16];
                               formatIpv4(ip, a, sizeof a);
                               const bool ok = store_.setBrokerIp(ip, settings_.mqtt_host);
                               Serial.printf("[mqtt] %s is at %s%s\n", settings_.mqtt_host, a,
                                             ok ? ", remembered" : ", could not save it");
                             }};
  if (!mqtt_.begin(cfg)) Serial.println("[mqtt] invalid settings; open the portal with 'portal'");
  Serial.printf("[mqtt] broker %s:%u, user %s\n", settings_.mqtt_host, settings_.mqtt_port, settings_.device_id);
  if (remembered != 0) {
    char a[16];
    formatIpv4(remembered, a, sizeof a);
    Serial.printf("[mqtt] remembered address %s, used if the name does not resolve\n", a);
  }

  schedule_ = TelemetrySchedule(kIntervalMs, millis());
  console_.begin();
}

void App::loop() {
  console_.poll();
  if (button_.poll()) {
    Serial.println("BOOT held 3 s: opening the setup portal");
    store_.setPortalRequested(true);
    restart(RestartCause::Portal);
  }

  wifi_.loop();
  if (wifi_.ups() != wifiUps_) {
    // Wi-Fi came back (or up for the first time): the mDNS responder starts over with it.
    if (wifiUps_ != 0) mqtt_.networkChanged();
    wifiUps_ = wifi_.ups();
  }
  mqtt_.loop(wifi_.connected());
  recover();
  if (static_cast<uint32_t>(millis() - recordAtMs_) >= kRecordEveryMs) {
    recordAtMs_ = millis();
    updateLinkRecord();
  }
  led_.setMode(ledModeFor(wifi_.connected(), mqtt_.connected()));

  if (schedule_.due(millis(), mqtt_.connected())) {
    sendAtMs_ = climate_.prepare();
    sendPending_ = true;
  }
  if (sendPending_ && static_cast<int32_t>(millis() - sendAtMs_) >= 0) {
    sendPending_ = false;
    sendTelemetry();
  }
  led_.update();
}

Reading App::readSensors() {
  const TempHumidity th = climate_.readPrepared();
  Reading r;
  r.temperature_c = th.temperature_c;
  r.humidity_pct = th.humidity_pct;
  r.lux = bh_.read();
  r.soil_raw = soil_.sampleRaw();
  return r;
}

void App::sendTelemetry() {
  const Reading r = readSensors();
  OptI rssi;
  if (wifi_.connected()) {
    rssi.ok = true;
    rssi.v = wifi_.rssi();
  }
  if (mqtt_.publishTelemetry(r, millis(), rssi)) {
    led_.flashPublished();
    Serial.printf("[telemetry] sent seq %lu, heap %lu\n", static_cast<unsigned long>(mqtt_.seq() - 1),
                  static_cast<unsigned long>(ESP.getFreeHeap()));
  } else {
    Serial.printf("[telemetry] not connected, reading dropped (skipped %lu)\n",
                  static_cast<unsigned long>(mqtt_.skipped()));
  }
}

void App::recover() {
  const uint32_t now = millis();
  const uint32_t sinceAck = mqtt_.msSinceAck(now);
  switch (recovery_.poll(now, sinceAck)) {
    case LinkRecovery::Action::None:
      break;
    case LinkRecovery::Action::ResetWifi:
      Serial.printf("[link] no acknowledged telemetry for %lu s, resetting Wi-Fi\n",
                    static_cast<unsigned long>(sinceAck / 1000));
      wifi_.reset();
      mqtt_.networkChanged();
      break;
    case LinkRecovery::Action::Restart:
      Serial.printf("[link] no acknowledged telemetry for %lu s, restarting\n",
                    static_cast<unsigned long>(sinceAck / 1000));
      restart(RestartCause::Supervisor);
  }
}

void App::updateLinkRecord() {
  LinkRecord& r = rtcRecord;
  const uint32_t now = millis();
  r.wifiStatus = static_cast<uint8_t>(WiFi.status());
  r.rssi = static_cast<int8_t>(wifi_.rssi());
  r.wifiReason = wifi_.lastDisconnectReason();
  r.mqttReason = mqtt_.lastDisconnectReason();
  r.resolveFailing = mqtt_.broker().failing() ? 1 : 0;
  r.uptimeS = now / 1000;
  r.sinceAckS = mqtt_.msSinceAck(now) / 1000;
  r.wifiDrops = wifi_.disconnects();
  r.wifiRetries = wifi_.retries();
  r.wifiResets = wifi_.resets();
  r.mqttConnects = mqtt_.connects();
  r.mqttDrops = mqtt_.drops();
  r.ackTimeouts = mqtt_.ackTimeouts();
  r.resolveFails = mqtt_.broker().failures();
  r.fallbacks = mqtt_.broker().fallbacks();
  sealLinkRecord(r);
}

void App::printLinkRecord(const LinkRecord& r, const char* indent, char* out, size_t cap) {
  const LinkRecordNames names{
      resetReasonName(r.resetReason), wifiStatusName(r.wifiStatus),
      r.wifiReason == kNoReason ? "" : WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(r.wifiReason))};
  formatLinkRecord(r, names, indent, out, cap);
}

void App::fillStatus(CliStatus& s) {
  s.device_id = settings_.device_id;
  s.boot_id = bootId_;
  s.mqtt_state = mqttStateName(mqtt_.state());
  s.seq = mqtt_.seq();
  s.skipped = mqtt_.skipped();
  updateLinkRecord();
  size_t n = strlcpy(linkText_, "this boot:\n", sizeof linkText_);
  printLinkRecord(rtcRecord, "  ", linkText_ + n, sizeof linkText_ - n);
  if (prevRecordValid_) {
    n = strlen(linkText_);
    n += strlcpy(linkText_ + n, "previous boot:\n", sizeof linkText_ - n);
    if (n < sizeof linkText_) printLinkRecord(prevRecord_, "  ", linkText_ + n, sizeof linkText_ - n);
  }
  s.link = linkText_;
}

void App::restart(RestartCause cause) {
  updateLinkRecord();
  rtcRecord.cause = static_cast<uint8_t>(cause);
  sealLinkRecord(rtcRecord);
  mqtt_.goOffline();
  Serial.flush();
  ESP.restart();
  for (;;) {
  }
}

}  // namespace olvar
