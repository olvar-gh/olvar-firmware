#include "app.h"

#include <esp_mac.h>
#include <esp_random.h>

#include "ids.h"
#include "pins.h"

namespace olvar {

namespace {

constexpr uint32_t kIntervalMs = OLVAR_INTERVAL_S * 1000UL;

}  // namespace

App::App()
    : sht_(bus_),
      bh_(bus_),
      store_(kv_),
      led_(pins::kLed),
      console_(Serial, sht_, bh_, soil_,
               CliHooks{[this](CliStatus& s) { fillStatus(s); },
                        [this] { store_.setPortalRequested(true); }, [this] { restart(); }}),
      schedule_(kIntervalMs, 0) {}

void App::setup() {
  Serial.begin(SerialCli::kBaud);
  Serial.printf("\nolvar-firmware %s\n", OLVAR_FW_VERSION);
  led_.begin();

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
  if (!sht_.begin()) Serial.println("[sensors] SHT31 not found at 0x44; temperature/humidity will be null");
  if (!bh_.begin()) Serial.println("[sensors] BH1750 not found at 0x23; lux will be null");
  soil_.begin();
  button_.begin();

  wifi_.begin(settings_.device_id);
  // esp_random() only draws on hardware entropy once the radio is running.
  bootIdFromRandom(esp_random(), bootId_);
  Serial.printf("boot_id %s\n", bootId_);

  const MqttLink::Config cfg{settings_.device_id, settings_.mqtt_pass, settings_.mqtt_host,
                             settings_.mqtt_port, bootId_, OLVAR_FW_VERSION};
  if (!mqtt_.begin(cfg)) Serial.println("[mqtt] invalid settings; open the portal with 'portal'");
  Serial.printf("[mqtt] broker %s:%u, user %s\n", settings_.mqtt_host, settings_.mqtt_port, settings_.device_id);

  schedule_ = TelemetrySchedule(kIntervalMs, millis());
  console_.begin();
}

void App::loop() {
  console_.poll();
  if (button_.poll()) {
    Serial.println("BOOT held 3 s: opening the setup portal");
    store_.setPortalRequested(true);
    restart();
  }

  wifi_.loop();
  mqtt_.loop(wifi_.connected());
  led_.setMode(ledModeFor(wifi_.connected(), mqtt_.connected()));

  if (schedule_.due(millis(), mqtt_.connected())) sendTelemetry();
  led_.update();
}

Reading App::readSensors() {
  const Sht31Result sht = sht_.read();
  Reading r;
  r.temperature_c = sht.temperature_c;
  r.humidity_pct = sht.humidity_pct;
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

void App::fillStatus(CliStatus& s) {
  s.device_id = settings_.device_id;
  s.boot_id = bootId_;
  s.mqtt_state = mqttStateName(mqtt_.state());
  s.seq = mqtt_.seq();
  s.skipped = mqtt_.skipped();
}

void App::restart() {
  mqtt_.goOffline();
  Serial.flush();
  ESP.restart();
  for (;;) {
  }
}

}  // namespace olvar
