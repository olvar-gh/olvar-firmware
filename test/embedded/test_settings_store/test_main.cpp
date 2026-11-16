// On-board tests for SettingsStore on real NVS. They erase the "olvar" namespace, so the board
// has to be provisioned again afterwards.
// Run: pio test -e esp32dev -f embedded/test_settings_store
#include <Arduino.h>
#include <esp_mac.h>
#include <string.h>
#include <unity.h>

#include "storage/settings_store.h"

using olvar::NvsKvStore;
using olvar::SettingsStore;

static NvsKvStore kv;
static SettingsStore store(kv);
static Settings defaults;

void setUp() { TEST_ASSERT_TRUE(store.clear()); }
void tearDown() {}

static Settings good() {
  Settings s;
  strcpy(s.device_id, "olvar-bench1");
  strcpy(s.mqtt_host, "192.168.1.20");
  s.mqtt_port = 1884;
  strcpy(s.mqtt_pass, "bench password");
  return s;
}

void test_empty_namespace_loads_defaults() {
  Settings out = good();
  TEST_ASSERT_FALSE(store.load(out, defaults));
  TEST_ASSERT_EQUAL_STRING(defaults.device_id, out.device_id);
  TEST_ASSERT_EQUAL_UINT16(defaults.mqtt_port, out.mqtt_port);
}

void test_round_trip() {
  TEST_ASSERT_TRUE(store.save(good()));
  Settings out;
  TEST_ASSERT_TRUE(store.load(out, defaults));
  TEST_ASSERT_EQUAL_STRING("olvar-bench1", out.device_id);
  TEST_ASSERT_EQUAL_STRING("192.168.1.20", out.mqtt_host);
  TEST_ASSERT_EQUAL_UINT16(1884, out.mqtt_port);
  TEST_ASSERT_EQUAL_STRING("bench password", out.mqtt_pass);
}

void test_missing_key_falls_back() {
  TEST_ASSERT_TRUE(store.save(good()));
  TEST_ASSERT_TRUE(kv.begin(SettingsStore::kNamespace, false));
  TEST_ASSERT_TRUE(kv.remove(SettingsStore::kKeyMqttPort));
  kv.end();
  Settings out;
  store.load(out, defaults);
  TEST_ASSERT_EQUAL_UINT16(defaults.mqtt_port, out.mqtt_port);
  TEST_ASSERT_EQUAL_STRING("olvar-bench1", out.device_id);
}

void test_portal_flag() {
  TEST_ASSERT_FALSE(store.portalRequested());
  TEST_ASSERT_TRUE(store.setPortalRequested(true));
  TEST_ASSERT_TRUE(store.portalRequested());
  TEST_ASSERT_TRUE(store.setPortalRequested(false));
  TEST_ASSERT_FALSE(store.portalRequested());
}

void test_clear_removes_everything() {
  TEST_ASSERT_TRUE(store.save(good()));
  TEST_ASSERT_TRUE(store.setPortalRequested(true));
  TEST_ASSERT_TRUE(store.clear());
  Settings out;
  TEST_ASSERT_FALSE(store.load(out, defaults));
  TEST_ASSERT_FALSE(store.portalRequested());
}

void setup() {
  delay(2000);  // let the serial monitor attach
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  defaults = olvar::defaultSettings(mac);
  UNITY_BEGIN();
  RUN_TEST(test_empty_namespace_loads_defaults);
  RUN_TEST(test_round_trip);
  RUN_TEST(test_missing_key_falls_back);
  RUN_TEST(test_portal_flag);
  RUN_TEST(test_clear_removes_everything);
  UNITY_END();
}

void loop() {}
