#include <string.h>
#include <unity.h>

#include "cli.h"
#include "soil_check.h"

// The pure half of src/serial_cli.cpp; its Arduino half is behind #ifdef ARDUINO.
#include "../../../src/serial_cli.cpp"

using olvar::LineReader;
using olvar::SoilCheck;
using Phase = olvar::SoilCheck::Phase;

void setUp() {}
void tearDown() {}

static int pushAll(LineReader& r, const char* s, const char** last) {
  int lines = 0;
  for (; *s; ++s) {
    if (r.push(*s)) {
      ++lines;
      *last = r.line();
    }
  }
  return lines;
}

static void test_line_reader_line_endings() {
  LineReader r;
  const char* last = nullptr;
  TEST_ASSERT_EQUAL_INT(1, pushAll(r, "status\r\n", &last));
  TEST_ASSERT_EQUAL_STRING("status", last);
  TEST_ASSERT_EQUAL_INT(1, pushAll(r, "raw\n", &last));
  TEST_ASSERT_EQUAL_STRING("raw", last);
  TEST_ASSERT_EQUAL_INT(1, pushAll(r, "help\r", &last));
  TEST_ASSERT_EQUAL_STRING("help", last);
}

static void test_line_reader_empty_enter_counts_once() {
  LineReader r;
  const char* last = nullptr;
  TEST_ASSERT_EQUAL_INT(1, pushAll(r, "\r\n", &last));
  TEST_ASSERT_EQUAL_STRING("", last);
  TEST_ASSERT_EQUAL_INT(2, pushAll(r, "\n\n", &last));
}

static void test_line_reader_overflow() {
  LineReader r;
  char longLine[LineReader::kMaxLen + 10];
  memset(longLine, 'x', sizeof longLine - 1);
  longLine[sizeof longLine - 1] = '\0';
  const char* last = nullptr;
  TEST_ASSERT_EQUAL_INT(0, pushAll(r, longLine, &last));
  TEST_ASSERT_TRUE(r.push('\n'));
  TEST_ASSERT_TRUE(r.overflowed());
  TEST_ASSERT_EQUAL_INT(1, pushAll(r, "status\n", &last));
  TEST_ASSERT_FALSE(r.overflowed());
  TEST_ASSERT_EQUAL_STRING("status", last);
}

struct FakeSource {
  const char* data;
  size_t pos = 0;
  int available() const { return static_cast<int>(strlen(data) - pos); }
  int read() { return data[pos] ? data[pos++] : -1; }
};

static void test_pump_stops_after_one_line() {
  LineReader r;
  FakeSource src{"raw\nraw\r\nstatus\n"};
  TEST_ASSERT_TRUE(r.pump(src, 128));
  TEST_ASSERT_EQUAL_STRING("raw", r.line());
  TEST_ASSERT_EQUAL_size_t(4, src.pos);
  TEST_ASSERT_TRUE(r.pump(src, 128));
  TEST_ASSERT_EQUAL_STRING("raw", r.line());
  TEST_ASSERT_TRUE(r.pump(src, 128));
  TEST_ASSERT_EQUAL_STRING("status", r.line());
  TEST_ASSERT_FALSE(r.pump(src, 128));
}

static void test_pump_respects_byte_budget() {
  LineReader r;
  FakeSource src{"status\n"};
  TEST_ASSERT_FALSE(r.pump(src, 3));
  TEST_ASSERT_EQUAL_size_t(3, src.pos);
  TEST_ASSERT_FALSE(r.pump(src, 3));
  TEST_ASSERT_EQUAL_size_t(6, src.pos);
  TEST_ASSERT_TRUE(r.pump(src, 3));
  TEST_ASSERT_FALSE(r.overflowed());
  TEST_ASSERT_EQUAL_STRING("status", r.line());
}

static OptI ok(int32_t v) { return OptI{true, v}; }

template <typename F>
static uint32_t runPhase(SoilCheck& c, uint32_t startMs, uint32_t durationMs, F value) {
  c.enter(startMs);
  uint32_t t = startMs;
  int i = 0;
  while (c.streaming()) {
    if (c.sampleDue(t)) c.addSample(t, ok(value(i++)));
    t += 100;
  }
  TEST_ASSERT_EQUAL_INT(static_cast<int>(durationMs / SoilCheck::kSampleEveryMs) + 1, i);
  return t;
}

static void test_soilcheck_good_sensor() {
  SoilCheck c;
  TEST_ASSERT_FALSE(c.active());
  c.start();
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Phase::WaitAir), static_cast<int>(c.phase()));
  TEST_ASSERT_FALSE(c.sampleDue(0));
  uint32_t t = runPhase(c, 1000, SoilCheck::kAirMs, [](int i) { return 3000 + (i % 3) * 10; });
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Phase::WaitWater), static_cast<int>(c.phase()));
  TEST_ASSERT_EQUAL_INT32(3000, c.air().min);
  TEST_ASSERT_EQUAL_INT32(3020, c.air().max);
  runPhase(c, t + 5000, SoilCheck::kWaterMs, [](int i) { return i < 3 ? 3000 - i * 600 : 1400; });
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Phase::Done), static_cast<int>(c.phase()));
  TEST_ASSERT_FALSE(c.active());
  TEST_ASSERT_EQUAL_INT32(1400, c.water().last);
  TEST_ASSERT_EQUAL_UINT32(3000, c.water().settleMs);
  TEST_ASSERT_EQUAL_INT32(c.air().last - 1400, c.span());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SoilVerdict::Ok), static_cast<int>(c.verdict()));
}

static void test_soilcheck_slow_settle_boundary() {
  SoilCheck c;
  c.start();
  uint32_t t = runPhase(c, 0, SoilCheck::kAirMs, [](int) { return 3000; });
  // Drops 100 counts per second until sample 15 (15 s), then flat: settles exactly at the limit.
  runPhase(c, t, SoilCheck::kWaterMs, [](int i) { return 2900 - 100 * (i < 15 ? i : 15); });
  TEST_ASSERT_EQUAL_UINT32(15000, c.water().settleMs);
  TEST_ASSERT_NOT_EQUAL(static_cast<int>(SoilVerdict::Slow), static_cast<int>(c.verdict()));

  c.start();
  t = runPhase(c, 0, SoilCheck::kAirMs, [](int) { return 3000; });
  runPhase(c, t, SoilCheck::kWaterMs, [](int i) { return 2900 - 100 * (i < 16 ? i : 16); });
  TEST_ASSERT_EQUAL_UINT32(16000, c.water().settleMs);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SoilVerdict::Slow), static_cast<int>(c.verdict()));
}

static void test_soilcheck_still_drifting_never_settles() {
  SoilCheck c;
  c.start();
  uint32_t t = runPhase(c, 0, SoilCheck::kAirMs, [](int) { return 3000; });
  runPhase(c, t, SoilCheck::kWaterMs, [](int i) { return 2900 - 40 * i; });
  TEST_ASSERT_EQUAL_UINT32(kSoilNeverSettled, c.water().settleMs);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SoilVerdict::Slow), static_cast<int>(c.verdict()));
}

static void test_soilcheck_skips_missed_slots() {
  SoilCheck c;
  c.start();
  c.enter(0);
  TEST_ASSERT_TRUE(c.sampleDue(0));
  TEST_ASSERT_EQUAL_UINT32(0, c.addSample(0, ok(3000)));
  TEST_ASSERT_FALSE(c.sampleDue(999));
  TEST_ASSERT_TRUE(c.sampleDue(1000));
  TEST_ASSERT_EQUAL_UINT32(4500, c.addSample(4500, ok(3000)));
  TEST_ASSERT_FALSE(c.sampleDue(4999));
  TEST_ASSERT_TRUE(c.sampleDue(5000));
}

static void test_soilcheck_failed_reads() {
  SoilCheck c;
  c.start();
  c.enter(0);
  for (uint32_t t = 0; c.streaming(); t += 1000) c.addSample(t, OptI{});
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Phase::Failed), static_cast<int>(c.phase()));
  TEST_ASSERT_FALSE(c.active());
}

static void test_soilcheck_abort_and_enter_outside_wait() {
  SoilCheck c;
  c.enter(0);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Phase::Idle), static_cast<int>(c.phase()));
  c.start();
  c.enter(0);
  c.enter(500);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Phase::Air), static_cast<int>(c.phase()));
  c.abort();
  TEST_ASSERT_FALSE(c.active());
  TEST_ASSERT_FALSE(c.sampleDue(5000));
}

static void test_every_verdict_has_name_and_advice() {
  const SoilVerdict all[] = {SoilVerdict::Ok,    SoilVerdict::Saturated, SoilVerdict::NoSignal,
                             SoilVerdict::Noisy, SoilVerdict::Swapped,   SoilVerdict::Stuck,
                             SoilVerdict::Slow,  SoilVerdict::SmallSpan};
  for (SoilVerdict v : all) {
    TEST_ASSERT_TRUE(strlen(olvar::soilVerdictName(v)) > 0);
    TEST_ASSERT_TRUE(strlen(olvar::soilVerdictAdvice(v)) > 20);
  }
  TEST_ASSERT_NOT_NULL(strstr(olvar::soilVerdictAdvice(SoilVerdict::Saturated), "3V3"));
  TEST_ASSERT_NOT_NULL(strstr(olvar::soilVerdictAdvice(SoilVerdict::Slow), "1 MOhm"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_line_reader_line_endings);
  RUN_TEST(test_line_reader_empty_enter_counts_once);
  RUN_TEST(test_line_reader_overflow);
  RUN_TEST(test_pump_stops_after_one_line);
  RUN_TEST(test_pump_respects_byte_budget);
  RUN_TEST(test_soilcheck_good_sensor);
  RUN_TEST(test_soilcheck_slow_settle_boundary);
  RUN_TEST(test_soilcheck_still_drifting_never_settles);
  RUN_TEST(test_soilcheck_skips_missed_slots);
  RUN_TEST(test_soilcheck_failed_reads);
  RUN_TEST(test_soilcheck_abort_and_enter_outside_wait);
  RUN_TEST(test_every_verdict_has_name_and_advice);
  return UNITY_END();
}
