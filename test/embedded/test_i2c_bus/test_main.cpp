// On-board tests for the shared I2C bus. Needs BH1750 @ 0x23 wired; the SHT31 (0x44/0x45) is
// optional because a DHT11 on GPIO4 can stand in for it (docs/WIRING.md).
// Run: pio test -e esp32dev -f embedded/test_i2c_bus
#include <Arduino.h>
#include <unity.h>

#include "sensors/i2c_bus.h"

using olvar::I2cBus;

static I2cBus bus;

void setUp() {}
void tearDown() {}

static bool contains(const uint8_t* a, size_t n, uint8_t v) {
  for (size_t i = 0; i < n; ++i)
    if (a[i] == v) return true;
  return false;
}

void test_scan_finds_the_sensors() {
  uint8_t found[16];
  size_t n = bus.scan(found, sizeof(found));
  if (n > sizeof(found)) n = sizeof(found);
  for (size_t i = 0; i < n; ++i) Serial.printf("  i2c device at 0x%02X\n", found[i]);
  if (!contains(found, n, olvar::pins::kSht31Addr) && !contains(found, n, olvar::pins::kSht31AltAddr)) {
    Serial.println("  no SHT31 at 0x44/0x45: fine if a DHT11 is wired to GPIO4 instead");
  }
  TEST_ASSERT_TRUE_MESSAGE(contains(found, n, olvar::pins::kBh1750Addr), "BH1750 0x23 not found");
}

void test_bus_runs_at_100khz() {
  TEST_ASSERT_EQUAL_UINT32(100000, bus.wire().getClock());
}

void test_recovery_after_three_consecutive_failures() {
  uint32_t before = bus.recoveries();
  bus.reportResult(false);
  bus.reportResult(false);
  TEST_ASSERT_EQUAL_UINT32(before, bus.recoveries());
  TEST_ASSERT_EQUAL_UINT8(2, bus.consecutiveFailures());
  bus.reportResult(false);
  TEST_ASSERT_EQUAL_UINT32(before + 1, bus.recoveries());
  TEST_ASSERT_EQUAL_UINT8(0, bus.consecutiveFailures());
  // The bus must work again after recovery.
  TEST_ASSERT_TRUE(bus.probe(olvar::pins::kBh1750Addr));
  TEST_ASSERT_EQUAL_UINT32(100000, bus.wire().getClock());
}

void test_success_resets_failure_count() {
  uint32_t before = bus.recoveries();
  bus.reportResult(false);
  bus.reportResult(false);
  bus.reportResult(true);
  bus.reportResult(false);
  bus.reportResult(false);
  TEST_ASSERT_EQUAL_UINT32(before, bus.recoveries());
  bus.reportResult(true);
}

void test_absent_address_does_not_ack() {
  TEST_ASSERT_FALSE(bus.probe(0x77));  // nothing on the Olvar bus uses 0x77
}

void setup() {
  delay(2000);  // let the serial monitor attach
  UNITY_BEGIN();
  TEST_ASSERT_TRUE(bus.begin());
  RUN_TEST(test_bus_runs_at_100khz);
  RUN_TEST(test_scan_finds_the_sensors);
  RUN_TEST(test_recovery_after_three_consecutive_failures);
  RUN_TEST(test_success_resets_failure_count);
  RUN_TEST(test_absent_address_does_not_ack);
  UNITY_END();
}

void loop() {}
