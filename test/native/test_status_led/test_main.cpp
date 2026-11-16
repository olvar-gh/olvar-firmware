#include <unity.h>

#include "../../../src/status_led.h"

using olvar::LedMode;
using olvar::LedPattern;

void setUp() {}
void tearDown() {}

static void test_no_wifi_blinks_slowly() {
  LedPattern p;
  p.setMode(LedMode::NoWifi, 0);
  TEST_ASSERT_TRUE(p.isOn(0));
  TEST_ASSERT_TRUE(p.isOn(999));
  TEST_ASSERT_FALSE(p.isOn(1000));
  TEST_ASSERT_FALSE(p.isOn(1999));
  TEST_ASSERT_TRUE(p.isOn(2000));
}

static void test_no_mqtt_blinks_fast() {
  LedPattern p;
  p.setMode(LedMode::NoMqtt, 5000);
  TEST_ASSERT_TRUE(p.isOn(5000));
  TEST_ASSERT_TRUE(p.isOn(5149));
  TEST_ASSERT_FALSE(p.isOn(5150));
  TEST_ASSERT_TRUE(p.isOn(5300));
}

static void test_online_is_dark_except_short_flash() {
  LedPattern p;
  p.setMode(LedMode::Online, 0);
  TEST_ASSERT_FALSE(p.isOn(10));
  p.flash(100);
  TEST_ASSERT_TRUE(p.isOn(100));
  TEST_ASSERT_TRUE(p.isOn(100 + LedPattern::kFlashMs - 1));
  TEST_ASSERT_FALSE(p.isOn(100 + LedPattern::kFlashMs));
  TEST_ASSERT_FALSE(p.isOn(5000));
}

static void test_mode_change_restarts_phase_and_same_mode_does_not() {
  LedPattern p;
  p.setMode(LedMode::Online, 0);
  p.setMode(LedMode::NoWifi, 1500);
  TEST_ASSERT_TRUE(p.isOn(1500));
  p.setMode(LedMode::NoWifi, 2600);
  TEST_ASSERT_FALSE(p.isOn(2600));
}

static void test_blink_survives_millis_wraparound() {
  LedPattern p;
  p.setMode(LedMode::Online, 0);
  p.setMode(LedMode::NoWifi, 0xFFFFFF00u);
  TEST_ASSERT_TRUE(p.isOn(0x00000100u));
  TEST_ASSERT_FALSE(p.isOn(0xFFFFFF00u + 1000));
  p.flash(0xFFFFFFF0u);
  TEST_ASSERT_TRUE(p.isOn(0x00000010u));
}

static void test_restart_starts_blink_on_regardless_of_uptime() {
  LedPattern p;
  TEST_ASSERT_FALSE(p.isOn(3500));
  p.restart(3500);
  TEST_ASSERT_TRUE(p.isOn(3500));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(LedMode::NoWifi), static_cast<int>(p.mode()));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_no_wifi_blinks_slowly);
  RUN_TEST(test_no_mqtt_blinks_fast);
  RUN_TEST(test_online_is_dark_except_short_flash);
  RUN_TEST(test_mode_change_restarts_phase_and_same_mode_does_not);
  RUN_TEST(test_blink_survives_millis_wraparound);
  RUN_TEST(test_restart_starts_blink_on_regardless_of_uptime);
  return UNITY_END();
}
