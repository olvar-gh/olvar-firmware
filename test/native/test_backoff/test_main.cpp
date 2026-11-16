#include <unity.h>

#include "backoff.h"

void setUp() {}
void tearDown() {}

static void test_doubles_from_min_to_max_without_jitter() {
  Backoff b(1000, 60000);
  const uint32_t expected[] = {1000, 2000, 4000, 8000, 16000, 32000, 60000, 60000};
  for (uint32_t e : expected) TEST_ASSERT_EQUAL_UINT32(e, b.next(500));
}

static void test_jitter_is_plus_minus_20_percent() {
  Backoff lo(1000, 60000), hi(1000, 60000);
  TEST_ASSERT_EQUAL_UINT32(800, lo.next(0));
  TEST_ASSERT_EQUAL_UINT32(1200, hi.next(1000));
  TEST_ASSERT_EQUAL_UINT32(1600, lo.next(0));
  TEST_ASSERT_EQUAL_UINT32(2400, hi.next(1000));
  Backoff big(1000, 60000);
  for (int i = 0; i < 10; ++i) big.next(500);
  TEST_ASSERT_EQUAL_UINT32(48000, big.next(0));
  TEST_ASSERT_EQUAL_UINT32(72000, big.next(1000));
  TEST_ASSERT_EQUAL_UINT32(72000, big.next(5000));  // clamped to 1000
}

static void test_every_jittered_value_in_range() {
  for (uint32_t r = 0; r < 1000; r += 37) {
    Backoff b(1000, 60000);
    uint32_t base = 1000;
    for (int i = 0; i < 10; ++i) {
      const uint32_t d = b.next(r);
      TEST_ASSERT_UINT32_WITHIN(base / 5, base, d);
      base = base * 2 > 60000 ? 60000 : base * 2;
    }
  }
}

static void test_reset_returns_to_min() {
  Backoff b(1000, 60000);
  for (int i = 0; i < 5; ++i) b.next(500);
  b.reset();
  TEST_ASSERT_EQUAL_UINT32(1000, b.next(500));
  TEST_ASSERT_EQUAL_UINT32(2000, b.next(500));
}

static void test_no_overflow_near_uint32_max() {
  Backoff b(0x80000000u, 0xFFFFFFFFu);
  TEST_ASSERT_EQUAL_UINT32(0x80000000u, b.next(500));
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, b.next(500));
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, b.next(500));
}

static void test_positive_jitter_saturates_near_uint32_max() {
  Backoff b(0x80000000u, 0xFFFFFFFFu);
  TEST_ASSERT_EQUAL_UINT32(0x80000000u, b.next(500));
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, b.next(1000));  // 1.2 * max would wrap without saturation
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, b.next(999));
  TEST_ASSERT_EQUAL_UINT32(0xCCCCCCCCu, b.next(0));     // negative jitter still applies
}

static void test_timer_first_call_fires_then_every_period() {
  IntervalTimer t(60000);
  TEST_ASSERT_TRUE(t.due(5));
  TEST_ASSERT_FALSE(t.due(5));
  TEST_ASSERT_FALSE(t.due(60004));
  TEST_ASSERT_TRUE(t.due(60005));
  TEST_ASSERT_FALSE(t.due(60006));
  TEST_ASSERT_TRUE(t.due(200000));  // late: fires once, no catch-up burst
  TEST_ASSERT_FALSE(t.due(200001));
}

static void test_timer_handles_millis_wraparound() {
  IntervalTimer t(0x200);
  TEST_ASSERT_TRUE(t.due(0xFFFFFF00u));
  TEST_ASSERT_FALSE(t.due(0xFFFFFFFFu));
  TEST_ASSERT_FALSE(t.due(0x00000000u));
  TEST_ASSERT_FALSE(t.due(0x000000FFu));
  TEST_ASSERT_TRUE(t.due(0x00000100u));
  TEST_ASSERT_FALSE(t.due(0x00000101u));
}

static void test_timer_restart_does_not_fire() {
  IntervalTimer t(1000);
  t.restart(0xFFFFFF00u);
  TEST_ASSERT_FALSE(t.due(0xFFFFFF00u));
  TEST_ASSERT_FALSE(t.due(0x000002E7u));
  TEST_ASSERT_TRUE(t.due(0x000002E8u));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_doubles_from_min_to_max_without_jitter);
  RUN_TEST(test_jitter_is_plus_minus_20_percent);
  RUN_TEST(test_every_jittered_value_in_range);
  RUN_TEST(test_reset_returns_to_min);
  RUN_TEST(test_no_overflow_near_uint32_max);
  RUN_TEST(test_positive_jitter_saturates_near_uint32_max);
  RUN_TEST(test_timer_first_call_fires_then_every_period);
  RUN_TEST(test_timer_handles_millis_wraparound);
  RUN_TEST(test_timer_restart_does_not_fire);
  return UNITY_END();
}
