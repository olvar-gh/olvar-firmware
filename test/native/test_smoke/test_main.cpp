// Placeholder native test: proves the host toolchain, Unity, olvar_core and
// ArduinoJson all build and link. Real tests arrive with T1-T3.
#include <ArduinoJson.h>
#include <string.h>
#include <unity.h>

#include "pins.h"
#include "reading.h"

void setUp() {}
void tearDown() {}

static void test_reading_defaults_are_null() {
  Reading r;
  TEST_ASSERT_FALSE(r.temperature_c.ok);
  TEST_ASSERT_FALSE(r.humidity_pct.ok);
  TEST_ASSERT_FALSE(r.lux.ok);
  TEST_ASSERT_FALSE(r.soil_raw.ok);
}

static void test_pins_match_plan() {
  TEST_ASSERT_EQUAL(21, olvar::pins::kI2cSda);
  TEST_ASSERT_EQUAL(22, olvar::pins::kI2cScl);
  TEST_ASSERT_EQUAL(34, olvar::pins::kSoil);
  TEST_ASSERT_EQUAL(2, olvar::pins::kLed);
  TEST_ASSERT_EQUAL(0, olvar::pins::kBoot);
  TEST_ASSERT_EQUAL(4, olvar::pins::kDht11);
  TEST_ASSERT_EQUAL_HEX8(0x44, olvar::pins::kSht31Addr);
  TEST_ASSERT_EQUAL_HEX8(0x45, olvar::pins::kSht31AltAddr);
  TEST_ASSERT_EQUAL_HEX8(0x23, olvar::pins::kBh1750Addr);
  TEST_ASSERT_EQUAL(60, OLVAR_INTERVAL_S);
}

static void test_fw_version_has_semver_prefix() {
  TEST_ASSERT_EQUAL(0, strncmp(OLVAR_FW_VERSION, "0.1.0", 5));
}

static void test_arduinojson_links() {
  JsonDocument doc;
  doc["x"] = nullptr;
  char out[16];
  serializeJson(doc, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("{\"x\":null}", out);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_reading_defaults_are_null);
  RUN_TEST(test_pins_match_plan);
  RUN_TEST(test_fw_version_has_semver_prefix);
  RUN_TEST(test_arduinojson_links);
  return UNITY_END();
}
