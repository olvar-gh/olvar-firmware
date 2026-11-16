#include <math.h>
#include <string.h>
#include <unity.h>

#include "backoff.h"
#include "pins.h"
#include "telemetry.h"
#include "../../../src/app.h"

using olvar::LedMode;
using olvar::MqttSession;
using olvar::TelemetrySchedule;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint32_t kPeriod = OLVAR_INTERVAL_S * 1000UL;

// Advances in 10 ms steps like loop() and returns the times at which due() fired.
template <size_t N>
size_t run(TelemetrySchedule& s, uint32_t from, uint32_t to, bool connected, uint32_t (&fired)[N]) {
  size_t n = 0;
  for (uint32_t t = from; t != to; t += 10) {
    if (s.due(t, connected) && n < N) fired[n++] = t;
  }
  return n;
}

}  // namespace

static void test_first_telemetry_within_5_s_of_connect_then_every_60_s() {
  TelemetrySchedule s(kPeriod, 0);
  uint32_t fired[8];
  TEST_ASSERT_EQUAL_size_t(0, run(s, 0, 7000, false, fired));
  size_t n = run(s, 7000, 7000 + 3 * kPeriod, true, fired);
  TEST_ASSERT_EQUAL_size_t(3, n);
  TEST_ASSERT_TRUE(fired[0] - 7000 <= 5000);
  TEST_ASSERT_EQUAL_UINT32(kPeriod, fired[1] - fired[0]);
  TEST_ASSERT_EQUAL_UINT32(kPeriod, fired[2] - fired[1]);
}

static void test_intervals_keep_running_while_disconnected() {
  TelemetrySchedule s(kPeriod, 0);
  uint32_t fired[8];
  size_t n = run(s, 0, 3 * kPeriod + 10, false, fired);
  TEST_ASSERT_EQUAL_size_t(3, n);
  TEST_ASSERT_EQUAL_UINT32(kPeriod, fired[0]);
  TEST_ASSERT_EQUAL_UINT32(2 * kPeriod, fired[1]);
}

static void test_reconnect_sends_a_fresh_reading_promptly() {
  TelemetrySchedule s(kPeriod, 0);
  uint32_t fired[8];
  run(s, 0, 100000, true, fired);
  run(s, 100000, 130000, false, fired);
  size_t n = run(s, 130000, 140000, true, fired);
  TEST_ASSERT_EQUAL_size_t(1, n);
  TEST_ASSERT_TRUE(fired[0] - 130000 <= 5000);
}

static void test_stall_longer_than_a_period_fires_once() {
  TelemetrySchedule s(kPeriod, 0);
  TEST_ASSERT_FALSE(s.due(1000, false));
  TEST_ASSERT_TRUE(s.due(5 * kPeriod, false));
  TEST_ASSERT_FALSE(s.due(5 * kPeriod + 10, false));
  TEST_ASSERT_TRUE(s.due(6 * kPeriod, false));
}

static void test_millis_wraparound() {
  const uint32_t start = 0xFFFFFF00u - kPeriod;
  TelemetrySchedule s(kPeriod, start);
  TEST_ASSERT_FALSE(s.due(0xFFFFFF00u - 1, false));
  TEST_ASSERT_TRUE(s.due(0xFFFFFF00u, false));
  TEST_ASSERT_FALSE(s.due(0x00000100u, false));
  TEST_ASSERT_TRUE(s.due(0xFFFFFF00u + kPeriod, false));
}

static void test_led_mode() {
  TEST_ASSERT_TRUE(olvar::ledModeFor(false, false) == LedMode::NoWifi);
  TEST_ASSERT_TRUE(olvar::ledModeFor(false, true) == LedMode::NoWifi);
  TEST_ASSERT_TRUE(olvar::ledModeFor(true, false) == LedMode::NoMqtt);
  TEST_ASSERT_TRUE(olvar::ledModeFor(true, true) == LedMode::Online);
}

static void test_mqtt_state_names() {
  TEST_ASSERT_EQUAL_STRING("disconnected", olvar::mqttStateName(MqttSession::State::Waiting));
  TEST_ASSERT_EQUAL_STRING("connecting", olvar::mqttStateName(MqttSession::State::Connecting));
  TEST_ASSERT_EQUAL_STRING("connected", olvar::mqttStateName(MqttSession::State::Connected));
}

static void test_unplugged_sensor_is_null_and_others_still_sent() {
  Reading r;
  r.temperature_c = OptF{false, 0};
  r.humidity_pct = OptF{true, NAN};
  r.lux = OptF{true, 312.5f};
  r.soil_raw = OptI{true, 2210};
  const Meta m{"olvar-a1b2c3", "9f3c07ab", 7, 1000, "0.1.0+test", OptI{}};
  char out[320];
  TEST_ASSERT_TRUE(buildTelemetryJson(r, m, out, sizeof out) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"temperature_c\":null"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"humidity_pct\":null"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"lux\":312.5"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"soil_raw\":2210"));
  TEST_ASSERT_NULL(strstr(out, "nan"));
  TEST_ASSERT_NULL(strstr(out, "NaN"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_first_telemetry_within_5_s_of_connect_then_every_60_s);
  RUN_TEST(test_intervals_keep_running_while_disconnected);
  RUN_TEST(test_reconnect_sends_a_fresh_reading_promptly);
  RUN_TEST(test_stall_longer_than_a_period_fires_once);
  RUN_TEST(test_millis_wraparound);
  RUN_TEST(test_led_mode);
  RUN_TEST(test_mqtt_state_names);
  RUN_TEST(test_unplugged_sensor_is_null_and_others_still_sent);
  return UNITY_END();
}
