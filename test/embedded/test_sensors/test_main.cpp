// On-board sensor checks (docs/PLAN.md T4). Needs the wiring in docs/WIRING.md.
// The lux-covered and soil air/water checks need a hand action. PlatformIO's test runner
// only reads the serial port (it never forwards keyboard input), so each step is a timed
// phase: run `pio test -e esp32dev -f embedded/test_sensors -v`, watch the countdown and do
// the action before it reaches 0.
#include <Arduino.h>
#include <unity.h>

#include "sensors/bh1750_sensor.h"
#include "sensors/i2c_bus.h"
#include "sensors/climate_sensor.h"
#include "sensors/soil_sensor.h"

using namespace olvar;

static I2cBus bus;
static ClimateSensor climate(bus);
static Bh1750Sensor bh(bus);
static SoilSensor soil;

static constexpr uint32_t kMaxCallMs = 200;

void setUp() {}
void tearDown() {}

// Announces a hand action and counts down so the user can do it before the measurement.
static void timedPhase(const char* action, uint32_t seconds) {
  Serial.printf("\n>>> %s NOW. Measuring in %lu s...\n", action, (unsigned long)seconds);
  for (uint32_t left = seconds; left > 0; --left) {
    Serial.printf("  %lu\n", (unsigned long)left);
    delay(1000);
  }
}

// SHT31: 5..40 C and 10..95 %RH. DHT11: its own coarse 1 C / 1 %RH part, rated 0..50 C and
// 20..90 %RH, so the check is that range.
void test_climate_in_room_range() {
  TEST_ASSERT_TRUE_MESSAGE(climate.kind() != ClimateSensor::Kind::None,
                           "no SHT31 at 0x44/0x45 and no DHT11 on GPIO4");
  bool dht = climate.kind() == ClimateSensor::Kind::Dht11;
  if (dht) {
    // The first read after the boot probe returns a sample taken before the DHT11 settled (one
    // gave 5 %RH there while later reads were normal): discard it and read again after 2 s.
    delay(Dht11Throttle::kMinIntervalMs);
    climate.read();
    delay(Dht11Throttle::kMinIntervalMs);
  }
  uint32_t t0 = millis();
  TempHumidity r = climate.read();
  uint32_t dt = millis() - t0;
  Serial.printf("  %s %.2f C %.2f %%RH (%lu ms)\n", climate.name(), r.temperature_c.v, r.humidity_pct.v,
                (unsigned long)dt);
  TEST_ASSERT_TRUE(r.temperature_c.ok);
  TEST_ASSERT_TRUE(r.humidity_pct.ok);
  if (dht) {
    TEST_ASSERT_FLOAT_WITHIN(25.0f, 25.0f, r.temperature_c.v);  // 0..50 C
    TEST_ASSERT_FLOAT_WITHIN(35.0f, 55.0f, r.humidity_pct.v);   // 20..90 %RH
  } else {
    TEST_ASSERT_FLOAT_WITHIN(17.5f, 22.5f, r.temperature_c.v);  // 5..40 C
    TEST_ASSERT_FLOAT_WITHIN(42.5f, 52.5f, r.humidity_pct.v);   // 10..95 %RH
  }
  TEST_ASSERT_LESS_THAN_UINT32(kMaxCallMs, dt);
}

// A second read inside 2 s must not touch the DHT11: same values, no 20 ms start pulse.
void test_dht11_second_read_within_2s_is_cached() {
  if (climate.kind() != ClimateSensor::Kind::Dht11) TEST_IGNORE_MESSAGE("SHT31 in use");
  TempHumidity a = climate.read();
  uint32_t t0 = millis();
  TempHumidity b = climate.read();
  TEST_ASSERT_LESS_THAN_UINT32(5, millis() - t0);
  TEST_ASSERT_EQUAL(a.temperature_c.ok, b.temperature_c.ok);
  TEST_ASSERT_EQUAL_FLOAT(a.temperature_c.v, b.temperature_c.v);
  TEST_ASSERT_EQUAL_FLOAT(a.humidity_pct.v, b.humidity_pct.v);
}

void test_sht31_wrong_address_gives_not_ok() {
  Sht31Sensor absent(bus, 0x46);  // nothing on the Olvar bus uses 0x46
  uint32_t t0 = millis();
  TempHumidity r = absent.read();
  TEST_ASSERT_LESS_THAN_UINT32(kMaxCallMs, millis() - t0);
  TEST_ASSERT_FALSE(r.temperature_c.ok);
  TEST_ASSERT_FALSE(r.humidity_pct.ok);
  bus.reportResult(true);  // don't let this test's failures trigger a recovery later
}

// GPIO27 is free on the Olvar board: no sensor answers there, so the read times out quickly.
void test_dht11_absent_gives_not_ok_quickly() {
  Dht11Sensor absent(27);
  absent.begin();
  uint32_t t0 = millis();
  TempHumidity r = absent.read();
  TEST_ASSERT_LESS_THAN_UINT32(kMaxCallMs, millis() - t0);
  TEST_ASSERT_FALSE(r.temperature_c.ok);
  TEST_ASSERT_FALSE(r.humidity_pct.ok);
}

void test_bh1750_room_light() {
  uint32_t t0 = millis();
  OptF lux = bh.read();
  uint32_t dt = millis() - t0;
  Serial.printf("  BH1750 %.1f lx (%lu ms)\n", lux.v, (unsigned long)dt);
  TEST_ASSERT_TRUE(lux.ok);
  TEST_ASSERT_TRUE(lux.v > 0);
  TEST_ASSERT_LESS_THAN_UINT32(kMaxCallMs, dt);
}

void test_bh1750_first_read_without_begin_under_budget() {
  Bh1750Sensor fresh(bus);
  uint32_t t0 = millis();
  OptF lux = fresh.read();
  uint32_t dt = millis() - t0;
  Serial.printf("  BH1750 first read %.1f lx (%lu ms)\n", lux.v, (unsigned long)dt);
  TEST_ASSERT_TRUE(lux.ok);
  TEST_ASSERT_LESS_THAN_UINT32(kMaxCallMs, dt);
}

void test_bh1750_covered() {
  timedPhase("Cover the GY-302 window with your finger and keep it covered", 10);
  OptF lux = bh.read();
  Serial.printf("  BH1750 covered %.1f lx\n", lux.v);
  TEST_ASSERT_TRUE(lux.ok);
  TEST_ASSERT_TRUE(lux.v <= 5.0f);
}

void test_bh1750_wrong_address_gives_not_ok() {
  Bh1750Sensor absent(bus, 0x5C);
  uint32_t t0 = millis();
  TEST_ASSERT_FALSE(absent.read().ok);
  TEST_ASSERT_LESS_THAN_UINT32(kMaxCallMs, millis() - t0);
  bus.reportResult(true);
}

void test_soil_sample_in_range_and_fast() {
  uint32_t t0 = millis();
  OptI raw = soil.sampleRaw();
  uint32_t dt = millis() - t0;
  Serial.printf("  soil_raw %ld (%lu ms)\n", (long)raw.v, (unsigned long)dt);
  TEST_ASSERT_TRUE(raw.ok);
  TEST_ASSERT_TRUE(raw.v >= 0 && raw.v <= 4095);
  TEST_ASSERT_LESS_THAN_UINT32(kMaxCallMs, dt);
}

void test_soil_air_vs_water() {
  timedPhase("Uncover the light sensor. Hold the dry soil sensor in the AIR", 15);
  OptI air = soil.sampleRaw();
  // 20 s: time to move it plus the 15 s the plan allows the reading to settle.
  timedPhase("Stand the soil blade in a glass of WATER up to the line", 20);
  OptI water = soil.sampleRaw();
  Serial.printf("  soil air %ld water %ld span %ld\n", (long)air.v, (long)water.v, (long)(air.v - water.v));
  TEST_ASSERT_TRUE(air.ok && water.ok);
  TEST_ASSERT_GREATER_THAN_INT32(500, air.v - water.v);
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  UNITY_BEGIN();
  TEST_ASSERT_TRUE(bus.begin());
  climate.begin();
  bh.begin();
  soil.begin();
  RUN_TEST(test_climate_in_room_range);
  RUN_TEST(test_dht11_second_read_within_2s_is_cached);
  RUN_TEST(test_sht31_wrong_address_gives_not_ok);
  RUN_TEST(test_dht11_absent_gives_not_ok_quickly);
  RUN_TEST(test_bh1750_room_light);
  RUN_TEST(test_bh1750_first_read_without_begin_under_budget);
  RUN_TEST(test_bh1750_wrong_address_gives_not_ok);
  RUN_TEST(test_soil_sample_in_range_and_fast);
  RUN_TEST(test_bh1750_covered);
  RUN_TEST(test_soil_air_vs_water);
  UNITY_END();
}

void loop() {}
