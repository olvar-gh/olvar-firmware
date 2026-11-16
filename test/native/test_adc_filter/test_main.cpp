#include <unity.h>

#include "adc_filter.h"

void setUp() {}
void tearDown() {}

static void test_n64_trim16_is_mean_of_middle_32() {
  int32_t s[64];
  // Middle 32 after sorting: 2000..2031 -> mean 2015.5 -> 2016 (half away from zero).
  for (int i = 0; i < 32; ++i) s[i] = 2031 - i;   // reversed so sorting matters
  for (int i = 0; i < 16; ++i) s[32 + i] = 0;      // low outliers
  for (int i = 0; i < 16; ++i) s[48 + i] = 4095;   // high outliers
  TEST_ASSERT_EQUAL_INT32(2016, trimmedMean(s, 64, 16));
}

static void test_outliers_are_ignored() {
  int32_t s[64];
  for (int i = 0; i < 64; ++i) s[i] = 1500;
  for (int i = 0; i < 16; ++i) s[i * 4] = (i % 2) ? 4095 : 0;  // 8 spikes high, 8 low
  TEST_ASSERT_EQUAL_INT32(1500, trimmedMean(s, 64, 16));
}

static void test_sorts_in_place() {
  int32_t s[5] = {5, 1, 4, 2, 3};
  trimmedMean(s, 5, 1);
  for (int i = 0; i < 5; ++i) TEST_ASSERT_EQUAL_INT32(i + 1, s[i]);
}

static void test_no_trim_is_plain_mean() {
  int32_t s[4] = {1, 2, 3, 5};  // 11/4 = 2.75 -> 3
  TEST_ASSERT_EQUAL_INT32(3, trimmedMean(s, 4, 0));
}

static void test_n1_returns_the_sample() {
  int32_t s[1] = {1234};
  TEST_ASSERT_EQUAL_INT32(1234, trimmedMean(s, 1, 0));
  TEST_ASSERT_EQUAL_INT32(1234, trimmedMean(s, 1, 16));
}

static void test_trim_at_half_falls_back_to_median_odd() {
  int32_t s[5] = {9, 100, 1, 50, 7};  // sorted 1 7 9 50 100 -> 9
  TEST_ASSERT_EQUAL_INT32(9, trimmedMean(s, 5, 2));
}

static void test_trim_just_below_half_is_trimmed_mean() {
  int32_t s[6] = {1, 10, 20, 30, 40, 1000};
  TEST_ASSERT_EQUAL_INT32(25, trimmedMean(s, 6, 2));  // (20+30)/2
  int32_t t[6] = {1, 10, 20, 30, 41, 1000};
  TEST_ASSERT_EQUAL_INT32(25, trimmedMean(t, 6, 1));  // (10+20+30+41)/4 = 25.25
}

static void test_trim_at_half_falls_back_to_median_even() {
  int32_t s[4] = {40, 10, 20, 31};  // sorted 10 20 31 40 -> (20+31)/2 = 25.5 -> 26
  TEST_ASSERT_EQUAL_INT32(26, trimmedMean(s, 4, 2));
  int32_t t[4] = {40, 10, 20, 31};
  TEST_ASSERT_EQUAL_INT32(26, trimmedMean(t, 4, 100));
}

static void test_negative_mean_rounds_away_from_zero() {
  int32_t s[2] = {-1, -2};  // -1.5 -> -2
  TEST_ASSERT_EQUAL_INT32(-2, trimmedMean(s, 2, 0));
}

static void test_empty_returns_zero() {
  TEST_ASSERT_EQUAL_INT32(0, trimmedMean(nullptr, 0, 0));
  int32_t s[1] = {7};
  TEST_ASSERT_EQUAL_INT32(0, trimmedMean(s, 0, 0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_n64_trim16_is_mean_of_middle_32);
  RUN_TEST(test_outliers_are_ignored);
  RUN_TEST(test_sorts_in_place);
  RUN_TEST(test_no_trim_is_plain_mean);
  RUN_TEST(test_n1_returns_the_sample);
  RUN_TEST(test_trim_at_half_falls_back_to_median_odd);
  RUN_TEST(test_trim_just_below_half_is_trimmed_mean);
  RUN_TEST(test_trim_at_half_falls_back_to_median_even);
  RUN_TEST(test_negative_mean_rounds_away_from_zero);
  RUN_TEST(test_empty_returns_zero);
  return UNITY_END();
}
