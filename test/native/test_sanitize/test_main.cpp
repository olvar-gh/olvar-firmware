#include <math.h>
#include <unity.h>

#include "sanitize.h"

void setUp() {}
void tearDown() {}

static void test_finite_passes_through() {
  OptF r = finiteOrNull(23.41f);
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_FLOAT(23.41f, r.v);
  r = finiteOrNull(0.0f);
  TEST_ASSERT_TRUE(r.ok);
  r = finiteOrNull(-40.0f);
  TEST_ASSERT_TRUE(r.ok);
}

static void test_nan_is_null() {
  TEST_ASSERT_FALSE(finiteOrNull(NAN).ok);
  TEST_ASSERT_FALSE(finiteOrNull(-NAN).ok);
}

static void test_inf_is_null() {
  TEST_ASSERT_FALSE(finiteOrNull(INFINITY).ok);
  TEST_ASSERT_FALSE(finiteOrNull(-INFINITY).ok);
}

static void test_round_half_away_from_zero() {
  TEST_ASSERT_EQUAL_FLOAT(3.0f, roundTo(2.5f, 0));
  TEST_ASSERT_EQUAL_FLOAT(-3.0f, roundTo(-2.5f, 0));
  TEST_ASSERT_EQUAL_FLOAT(2.0f, roundTo(2.4f, 0));
  TEST_ASSERT_EQUAL_FLOAT(0.13f, roundTo(0.125f, 2));   // 0.125 is exact in binary
  TEST_ASSERT_EQUAL_FLOAT(-0.13f, roundTo(-0.125f, 2));
}

static void test_round_typical_fields() {
  TEST_ASSERT_EQUAL_FLOAT(23.41f, roundTo(23.4149f, 2));
  TEST_ASSERT_EQUAL_FLOAT(51.07f, roundTo(51.0712f, 2));
  TEST_ASSERT_EQUAL_FLOAT(312.5f, roundTo(312.54f, 1));
}

static void test_round_clamps_decimals_and_keeps_non_finite() {
  TEST_ASSERT_EQUAL_FLOAT(3.0f, roundTo(2.5f, -1));
  TEST_ASSERT_EQUAL_FLOAT(1.234567f, roundTo(1.2345671f, 9));
  TEST_ASSERT_TRUE(isnan(roundTo(NAN, 2)));
  TEST_ASSERT_TRUE(isinf(roundTo(INFINITY, 2)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_finite_passes_through);
  RUN_TEST(test_nan_is_null);
  RUN_TEST(test_inf_is_null);
  RUN_TEST(test_round_half_away_from_zero);
  RUN_TEST(test_round_typical_fields);
  RUN_TEST(test_round_clamps_decimals_and_keeps_non_finite);
  return UNITY_END();
}
