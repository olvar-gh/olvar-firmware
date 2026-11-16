// One test per §7.3 verdict, each with a boundary value on both sides of its threshold.
#include <unity.h>

#include "soil_check.h"

void setUp() {}
void tearDown() {}

// A healthy run: air 3000 (steady), water 1500 settled after 5 s -> span 1500, Ok.
static SoilWindow air(int32_t min = 2990, int32_t max = 3010, int32_t last = 3000) {
  return SoilWindow{min, max, last, 0};
}
static SoilWindow water(int32_t last = 1500, uint32_t settleMs = 5000) {
  return SoilWindow{last - 10, last + 10, last, settleMs};
}

#define ASSERT_VERDICT(expected, a, w) TEST_ASSERT_EQUAL_INT((int)(expected), (int)judgeSoilCheck(a, w))

static void test_thresholds_match_plan() {
  TEST_ASSERT_EQUAL_INT32(4000, kSoilSaturatedMin);
  TEST_ASSERT_EQUAL_INT32(300, kSoilNoSignalMax);
  TEST_ASSERT_EQUAL_INT32(300, kSoilNoisyRange);
  TEST_ASSERT_EQUAL_INT32(150, kSoilSwappedMargin);
  TEST_ASSERT_EQUAL_INT32(150, kSoilStuckSpan);
  TEST_ASSERT_EQUAL_UINT32(15000, kSoilSettleLimitMs);
  TEST_ASSERT_EQUAL_INT32(30, kSoilSettleBandCounts);
  TEST_ASSERT_EQUAL_INT32(600, kSoilOkSpan);
}

static void test_ok() { ASSERT_VERDICT(SoilVerdict::Ok, air(), water()); }

static void test_saturated_boundary() {
  ASSERT_VERDICT(SoilVerdict::Saturated, air(4000, 4095, 4095), water());
  // 3999: not saturated; falls through to the next rules (Ok here).
  ASSERT_VERDICT(SoilVerdict::Ok, air(3999, 4095, 4095), water(2500));
}

static void test_no_signal_boundary() {
  ASSERT_VERDICT(SoilVerdict::NoSignal, air(0, 299, 150), water(100));
  // max 300: has a signal; with this water value it is Stuck, not NoSignal.
  ASSERT_VERDICT(SoilVerdict::Stuck, air(150, 300, 200), water(100));
}

static void test_noisy_boundary() {
  ASSERT_VERDICT(SoilVerdict::Noisy, air(2800, 3101, 3000), water());
  ASSERT_VERDICT(SoilVerdict::Ok, air(2800, 3100, 3000), water());
}

static void test_swapped_boundary() {
  ASSERT_VERDICT(SoilVerdict::Swapped, air(1490, 1510, 1500), water(1651));
  // water == air + 150: not swapped; span -150 < 150 -> Stuck.
  ASSERT_VERDICT(SoilVerdict::Stuck, air(1490, 1510, 1500), water(1650));
}

static void test_stuck_boundary() {
  ASSERT_VERDICT(SoilVerdict::Stuck, air(), water(3000 - 149));
  // span 150: reacts, but weakly.
  ASSERT_VERDICT(SoilVerdict::SmallSpan, air(), water(3000 - 150));
}

static void test_slow_boundary() {
  ASSERT_VERDICT(SoilVerdict::Slow, air(), water(1500, 15001));
  ASSERT_VERDICT(SoilVerdict::Ok, air(), water(1500, 15000));
}

static void test_slow_when_still_drifting_at_end() {
  ASSERT_VERDICT(SoilVerdict::Slow, air(), water(1500, kSoilNeverSettled));
}

static void test_small_span_boundary() {
  ASSERT_VERDICT(SoilVerdict::SmallSpan, air(), water(3000 - 599));
  ASSERT_VERDICT(SoilVerdict::Ok, air(), water(3000 - 600));
}

static void test_rule_order() {
  // Saturated beats Noisy; Noisy beats Swapped; Stuck beats Slow; Slow beats SmallSpan.
  ASSERT_VERDICT(SoilVerdict::Saturated, air(4000, 4095, 4095), water(4095, kSoilNeverSettled));
  ASSERT_VERDICT(SoilVerdict::Noisy, air(1000, 1400, 1200), water(3000));
  ASSERT_VERDICT(SoilVerdict::Stuck, air(), water(2900, kSoilNeverSettled));
  ASSERT_VERDICT(SoilVerdict::Slow, air(), water(2700, kSoilNeverSettled));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_thresholds_match_plan);
  RUN_TEST(test_ok);
  RUN_TEST(test_saturated_boundary);
  RUN_TEST(test_no_signal_boundary);
  RUN_TEST(test_noisy_boundary);
  RUN_TEST(test_swapped_boundary);
  RUN_TEST(test_stuck_boundary);
  RUN_TEST(test_slow_boundary);
  RUN_TEST(test_slow_when_still_drifting_at_end);
  RUN_TEST(test_small_span_boundary);
  RUN_TEST(test_rule_order);
  return UNITY_END();
}
