#include <string.h>
#include <unity.h>

#include "cli.h"
#include "soil_check.h"

// The pure half of src/serial_cli.cpp; its Arduino half is behind #ifdef ARDUINO.
#include "../../../src/serial_cli.cpp"

using olvar::LineReader;
using olvar::DhtWatch;
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

static TempHumidity th(float h) {
  TempHumidity r;
  r.humidity_pct = {true, h};
  r.temperature_c = {true, 25};
  return r;
}

// dhtwatch reads whenever the DHT11 itself allows (2 s after the previous transaction) and
// stops at the first read 30 s or more after start; a breath shows up as min/max.
static void test_dhtwatch_paces_by_the_sensor_and_ends_after_30s() {
  DhtWatch w;
  TEST_ASSERT_FALSE(w.due(0, 0));
  w.start(1000);
  TEST_ASSERT_TRUE(w.due(1000, 1000));
  TEST_ASSERT_FALSE(w.due(1000, 1500));
  uint32_t t = 1000;
  float h[] = {5, 6, 5, 22, 41, 30, 18, 11, 8, 7, 6, 6, 5, 5, 5, 6};
  int lines = 0;
  while (w.active()) {
    TEST_ASSERT_TRUE(lines < 16);
    TEST_ASSERT_EQUAL_UINT32(t - 1000, w.add(t, th(h[lines])));
    ++lines;
    t += 2025;  // ~25 ms transaction + 2 s pause
  }
  TEST_ASSERT_EQUAL_INT(16, lines);  // 0, 2.0, ..., 30.4 s
  TEST_ASSERT_EQUAL_UINT8(16, w.reads());
  TEST_ASSERT_EQUAL_UINT8(0, w.failed());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 5, w.humidityMin().v);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 41, w.humidityMax().v);
  TEST_ASSERT_FALSE(w.due(t, 0));
}

static void test_dhtwatch_counts_failures_and_restarts_clean() {
  DhtWatch w;
  w.start(0);
  w.add(0, TempHumidity{});
  w.add(2000, th(40));
  TEST_ASSERT_EQUAL_UINT8(2, w.reads());
  TEST_ASSERT_EQUAL_UINT8(1, w.failed());
  w.abort();
  TEST_ASSERT_FALSE(w.active());
  TEST_ASSERT_EQUAL_UINT32(0, w.add(4000, th(50)));  // ignored once stopped
  TEST_ASSERT_EQUAL_UINT8(2, w.reads());
  w.start(10000);
  TEST_ASSERT_EQUAL_UINT8(0, w.reads());
  TEST_ASSERT_FALSE(w.humidityMin().ok);
  w.add(10000, TempHumidity{});
  TEST_ASSERT_FALSE(w.humidityMax().ok);  // no successful read yet
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
  RUN_TEST(test_dhtwatch_paces_by_the_sensor_and_ends_after_30s);
  RUN_TEST(test_dhtwatch_counts_failures_and_restarts_clean);
  return UNITY_END();
}
