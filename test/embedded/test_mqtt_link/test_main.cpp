// Runs on the board without Wi-Fi or a broker: checks the adapter's offline behaviour.
#include <Arduino.h>
#include <unity.h>

#include "net/mqtt_link.h"

using olvar::MqttLink;

void setUp() {}
void tearDown() {}

static MqttLink::Config config() {
  return MqttLink::Config{"olvar-a1b2c3", "pw", "olvar-gw.local", 1883, "9f3c07ab", "0.1.0+test"};
}

static void test_begin_rejects_oversized_fields() {
  MqttLink link;
  MqttLink::Config cfg = config();
  cfg.deviceId = "this-device-id-is-way-too-long-for-the-contract";
  TEST_ASSERT_FALSE(link.begin(cfg));
  cfg = config();
  cfg.host = nullptr;
  TEST_ASSERT_FALSE(link.begin(cfg));
  TEST_ASSERT_TRUE(link.begin(config()));
}

static void test_publish_while_disconnected_is_skipped_without_seq() {
  MqttLink link;
  TEST_ASSERT_TRUE(link.begin(config()));
  link.loop(false);
  Reading r;
  TEST_ASSERT_FALSE(link.publishTelemetry(r, millis(), OptI{}));
  TEST_ASSERT_FALSE(link.publishTelemetry(r, millis(), OptI{}));
  TEST_ASSERT_EQUAL_UINT32(0, link.seq());
  TEST_ASSERT_EQUAL_UINT32(2, link.skipped());
  TEST_ASSERT_FALSE(link.connected());
}

static void test_go_offline_returns_within_budget_when_disconnected() {
  MqttLink link;
  TEST_ASSERT_TRUE(link.begin(config()));
  const uint32_t start = millis();
  link.goOffline();
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(MqttLink::kGoOfflineMs + 20, millis() - start);
}

void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_begin_rejects_oversized_fields);
  RUN_TEST(test_publish_while_disconnected_is_skipped_without_seq);
  RUN_TEST(test_go_offline_returns_within_budget_when_disconnected);
  UNITY_END();
}

void loop() {}
