// DHT11 frame decoding and the 2 s read pacing (lib/olvar_core/src/dht11.*).
#include <string.h>
#include <unity.h>

#include <vector>

#include "dht11.h"

using olvar::Dht11Diag;
using olvar::Dht11Pulses;
using olvar::Dht11Stage;
using olvar::Dht11Throttle;
using olvar::dht11Decode;
using olvar::dht11PulsesToBytes;
using olvar::kDht11Bits;

void setUp() {}
void tearDown() {}

// 45 %RH, 23.4 C, checksum 72.
static const uint8_t kGood[5] = {45, 0, 23, 4, 72};

static void test_decodes_a_good_frame() {
  TempHumidity r = dht11Decode(kGood);
  TEST_ASSERT_TRUE(r.temperature_c.ok);
  TEST_ASSERT_TRUE(r.humidity_pct.ok);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 23.4f, r.temperature_c.v);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 45.0f, r.humidity_pct.v);
}

static void test_bad_checksum_gives_null() {
  const uint8_t bytes[5] = {45, 0, 23, 4, 73};
  TempHumidity r = dht11Decode(bytes);
  TEST_ASSERT_FALSE(r.temperature_c.ok);
  TEST_ASSERT_FALSE(r.humidity_pct.ok);
}

static void test_all_zero_frame_gives_null() {
  const uint8_t bytes[5] = {0, 0, 0, 0, 0};
  TEST_ASSERT_FALSE(dht11Decode(bytes).temperature_c.ok);
}

static void test_negative_temperature() {
  const uint8_t bytes[5] = {60, 0, 5, 0x82, static_cast<uint8_t>(60 + 5 + 0x82)};
  TempHumidity r = dht11Decode(bytes);
  TEST_ASSERT_TRUE(r.temperature_c.ok);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -5.2f, r.temperature_c.v);
}

// Aosong DHT11 datasheet (2022-12) §5.2 example: 52.1 %RH, -24.12 C.
static void test_datasheet_example_frame() {
  const uint8_t bytes[5] = {0x34, 0x01, 0x18, 0x8C, 0xD9};
  TempHumidity r = dht11Decode(bytes);
  TEST_ASSERT_TRUE(r.temperature_c.ok);
  TEST_ASSERT_TRUE(r.humidity_pct.ok);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 52.1f, r.humidity_pct.v);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -24.12f, r.temperature_c.v);
}

static void test_two_digit_fractions() {
  const uint8_t bytes[5] = {40, 25, 21, 7, static_cast<uint8_t>(40 + 25 + 21 + 7)};
  TempHumidity r = dht11Decode(bytes);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 40.25f, r.humidity_pct.v);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 21.7f, r.temperature_c.v);
}

static void test_out_of_range_gives_null() {
  const uint8_t wetter[5] = {101, 0, 20, 0, 121};
  TEST_ASSERT_FALSE(dht11Decode(wetter).humidity_pct.ok);
  const uint8_t hot[5] = {40, 0, 81, 0, 121};
  TEST_ASSERT_FALSE(dht11Decode(hot).temperature_c.ok);
}

static void test_pulses_to_bytes_uses_50us_threshold() {
  uint16_t pulses[kDht11Bits];
  for (size_t i = 0; i < kDht11Bits; ++i) {
    bool one = (kGood[i / 8] >> (7 - i % 8)) & 1;
    pulses[i] = one ? 70 : 27;
  }
  pulses[39] = 50;  // checksum LSB: exactly at the threshold counts as 1 (72 -> 73)
  uint8_t bytes[5];
  dht11PulsesToBytes(pulses, bytes);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kGood, bytes, 4);
  TEST_ASSERT_EQUAL_UINT8(73, bytes[4]);
  pulses[39] = 49;
  dht11PulsesToBytes(pulses, bytes);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kGood, bytes, 5);
}

// The ASAIR part reports 5 %RH at the bottom of its 5-95 %RH range. A frame that says so with a
// valid checksum is passed through as is: the firmware neither makes nor hides that value.
static void test_floor_humidity_frame_passes_through() {
  const uint8_t bytes[5] = {0x05, 0x00, 0x19, 0x03, 0x21};
  TempHumidity r = dht11Decode(bytes);
  TEST_ASSERT_TRUE(r.humidity_pct.ok);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, r.humidity_pct.v);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.3f, r.temperature_c.v);
}

// A misread data bit never yields a plausible wrong value: the checksum rejects every single-bit
// error, so a timing fault shows up as null, not as a steady low humidity.
static void test_every_single_bit_error_is_rejected() {
  for (size_t bit = 0; bit < kDht11Bits; ++bit) {
    uint8_t bytes[5];
    memcpy(bytes, kGood, 5);
    bytes[bit / 8] ^= static_cast<uint8_t>(0x80 >> (bit % 8));
    TEST_ASSERT_FALSE_MESSAGE(dht11Decode(bytes).humidity_pct.ok, "single-bit error accepted");
  }
}

// A simulated bus: one entry per microsecond, each level() call advances the clock by 1 us.
struct FakeLine {
  std::vector<uint8_t> wave;
  size_t t = 0;
  int idle = 1;
  int level() {
    int l = t < wave.size() ? wave[t] : idle;
    ++t;
    return l;
  }
  uint32_t us() { return static_cast<uint32_t>(t); }
  void add(int level, int us) { wave.insert(wave.end(), us, static_cast<uint8_t>(level)); }
};

// The line right after the host releases it: riseUs still low, then a datasheet reply with
// `bits` data bits of `bytes` (0: 24 us high, 1: 71 us high).
static FakeLine reply(const uint8_t bytes[5], int riseUs, size_t bits = kDht11Bits) {
  FakeLine line;
  line.add(0, riseUs);
  line.add(1, 13);  // host release, Tgo
  line.add(0, 83);  // response low
  line.add(1, 87);  // response high
  for (size_t i = 0; i < bits; ++i) {
    line.add(0, 54);
    line.add(1, (bytes[i / 8] >> (7 - i % 8)) & 1 ? 71 : 24);
  }
  line.add(0, 54);  // end of frame, then the pull-up
  return line;
}

static void test_walk_reads_a_whole_reply() {
  FakeLine line = reply(kGood, 0);
  Dht11Pulses p;
  olvar::dht11WalkReply(line, p);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Dht11Stage::Done), static_cast<int>(p.stage));
  TEST_ASSERT_EQUAL_UINT8(kDht11Bits, p.bits);
  uint8_t bytes[5];
  dht11PulsesToBytes(p.highUs, bytes);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kGood, bytes, 5);
}

// The line can still read low for a moment after the release while the pull-up raises it. The
// old walk took that for the response, timed the 87 us response high as bit 0 and shifted the
// frame by one bit (always a bad checksum). Now the rise is waited out first.
static void test_walk_waits_for_the_release_to_rise() {
  for (int rise : {1, 3, 10}) {
    FakeLine line = reply(kGood, rise);
    Dht11Pulses p;
    olvar::dht11WalkReply(line, p);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Dht11Stage::Done), static_cast<int>(p.stage));
    uint8_t bytes[5];
    dht11PulsesToBytes(p.highUs, bytes);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kGood, bytes, 5);
  }
}

static void test_walk_line_held_low() {
  FakeLine line;
  line.idle = 0;
  Dht11Pulses p;
  olvar::dht11WalkReply(line, p);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Dht11Stage::Release), static_cast<int>(p.stage));
}

static void test_walk_no_sensor() {
  FakeLine line;  // pull-up only
  Dht11Pulses p;
  olvar::dht11WalkReply(line, p);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Dht11Stage::NoResponse), static_cast<int>(p.stage));
  TEST_ASSERT_EQUAL_UINT8(0, p.bits);
}

static void test_walk_stops_at_a_missing_bit() {
  FakeLine line = reply(kGood, 0, 17);
  line.idle = 0;  // the sensor stops mid-frame and the line stays low
  Dht11Pulses p;
  olvar::dht11WalkReply(line, p);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Dht11Stage::Data), static_cast<int>(p.stage));
  TEST_ASSERT_EQUAL_UINT8(17, p.bits);  // 17 bits timed, then the low phase never ends
}

static Dht11Pulses pulsesFor(const uint8_t bytes[5], uint16_t zeroUs, uint16_t oneUs) {
  Dht11Pulses p;
  p.stage = Dht11Stage::Done;
  p.bits = kDht11Bits;
  for (size_t i = 0; i < kDht11Bits; ++i) p.highUs[i] = (bytes[i / 8] >> (7 - i % 8)) & 1 ? oneUs : zeroUs;
  return p;
}

static void test_diagnose_reports_bytes_checksum_and_widths() {
  Dht11Pulses p = pulsesFor(kGood, 24, 70);
  p.highUs[0] = 21;  // a 0 bit
  p.highUs[2] = 74;  // a 1 bit (45 = 0b00101101)
  Dht11Diag d = olvar::dht11Diagnose(p);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kGood, d.bytes, 5);
  TEST_ASSERT_TRUE(d.checksumOk);
  TEST_ASSERT_EQUAL_UINT16(21, d.zeroMinUs);
  TEST_ASSERT_EQUAL_UINT16(24, d.zeroMaxUs);
  TEST_ASSERT_EQUAL_UINT16(70, d.oneMinUs);
  TEST_ASSERT_EQUAL_UINT16(74, d.oneMaxUs);
  TEST_ASSERT_EQUAL_UINT8(kDht11Bits, d.zeros + d.ones);

  const uint8_t bad[5] = {45, 0, 23, 4, 73};
  TEST_ASSERT_FALSE(olvar::dht11Diagnose(pulsesFor(bad, 24, 70)).checksumOk);
}

static void test_format_good_frame() {
  const uint8_t bytes[5] = {0x05, 0x00, 0x19, 0x03, 0x21};
  Dht11Diag d = olvar::dht11Diagnose(pulsesFor(bytes, 24, 71));
  d.startLowUs = 20105;
  char out[128];
  size_t n = olvar::formatDht11Diag(d, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("bytes=05 00 19 03 21 checksum=ok bit0_us=24..24 bit1_us=71..71 start_us=20105", out);
  TEST_ASSERT_EQUAL_size_t(strlen(out), n);
}

static void test_format_errors() {
  Dht11Diag d;
  d.startLowUs = 20100;
  d.stage = Dht11Stage::NoResponse;
  char out[128];
  olvar::formatDht11Diag(d, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("error=no_response bits=0 bit0_us=none bit1_us=none start_us=20100", out);
  d.stage = Dht11Stage::Release;
  olvar::formatDht11Diag(d, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("error=line_low bits=0 bit0_us=none bit1_us=none start_us=20100", out);
  Dht11Pulses p = pulsesFor(kGood, 25, 72);
  p.stage = Dht11Stage::Data;
  p.bits = 16;
  d = olvar::dht11Diagnose(p);
  d.startLowUs = 20100;
  olvar::formatDht11Diag(d, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("error=bit_timeout bits=16 bit0_us=25..25 bit1_us=72..72 start_us=20100", out);
  d.stage = Dht11Stage::Response;
  olvar::formatDht11Diag(d, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING_LEN("error=response_cut", out, 18);
}

static void test_format_never_truncates() {
  Dht11Diag d = olvar::dht11Diagnose(pulsesFor(kGood, 24, 70));
  char out[20];
  TEST_ASSERT_EQUAL_size_t(0, olvar::formatDht11Diag(d, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("", out);
}

static void test_throttle_allows_one_read_per_2s() {
  Dht11Throttle t;
  TEST_ASSERT_TRUE(t.due(0));
  t.mark(500, true);
  TEST_ASSERT_FALSE(t.due(500));
  TEST_ASSERT_FALSE(t.due(2499));
  TEST_ASSERT_TRUE(t.due(2500));
}

// The deadline is anchored to the end of the transaction, not to when the caller asks.
static void test_throttle_due_at_is_absolute() {
  Dht11Throttle t;
  TEST_ASSERT_EQUAL_UINT32(100, t.dueAtMs(100));
  t.mark(1025, true);
  TEST_ASSERT_EQUAL_UINT32(3025, t.dueAtMs(1025));
  TEST_ASSERT_EQUAL_UINT32(3025, t.dueAtMs(2999));
  TEST_ASSERT_FALSE(t.due(t.dueAtMs(1025) - 1));
  TEST_ASSERT_TRUE(t.due(t.dueAtMs(1025)));
  TEST_ASSERT_EQUAL_UINT32(3100, t.dueAtMs(3100));
}

static void test_throttle_survives_millis_wrap() {
  Dht11Throttle t;
  t.mark(0xFFFFFF00u, true);
  TEST_ASSERT_FALSE(t.due(0x00000100u));
  TEST_ASSERT_TRUE(t.due(0x00000800u));
}

// Two answered transactions are needed before a reply holds a settled sample; failures don't count.
static void test_throttle_settles_after_two_answered_reads() {
  Dht11Throttle t;
  TEST_ASSERT_FALSE(t.nextReplySettled());
  t.mark(2000, true);  // boot probe: power-up sample
  TEST_ASSERT_FALSE(t.nextReplySettled());
  t.mark(4000, false);  // no answer
  TEST_ASSERT_FALSE(t.nextReplySettled());
  t.mark(6000, true);  // reply measured after the first start signal
  TEST_ASSERT_TRUE(t.nextReplySettled());
  t.mark(8000, false);  // a later glitch doesn't undo it
  TEST_ASSERT_TRUE(t.nextReplySettled());
}

// Mirrors ClimateSensor: prepare() reads the DHT11 if one is in use, readPrepared() reads again
// (detecting it if needed) and the gate decides whether that reply is published.
static void test_gate_nulls_dht11_first_detected_at_send_time() {
  olvar::ClimatePublishGate g;
  Dht11Throttle t;
  t.mark(2000, false);           // boot probe: no answer, kind stays None
  g.prepared(false, false, false);  // prepare() at 3990: still None
  t.mark(4010, true);            // readPrepared() at 4010: detect() finds the DHT11, power-up sample
  TEST_ASSERT_FALSE(g.publish(true));
  // Next interval: prepare() reads the post-start-signal sample, readPrepared() a settled one.
  t.mark(64000, true);
  g.prepared(true, true, t.nextReplySettled());
  TEST_ASSERT_TRUE(g.publish(true));
  // Without a new prepare() the DHT11 priming is used up.
  TEST_ASSERT_FALSE(g.publish(true));
}

static void test_gate_dht11_inside_boot_probe_window_is_null() {
  olvar::ClimatePublishGate g;
  Dht11Throttle t;
  t.mark(2000, true);  // boot probe answered
  g.prepared(true, true, t.nextReplySettled());  // prepare() within 2 s: no new transaction
  TEST_ASSERT_FALSE(g.publish(true));
}

static void test_gate_sht31_always_publishes_once_prepared() {
  olvar::ClimatePublishGate g;
  TEST_ASSERT_FALSE(g.publish(false));  // never prepared
  g.prepared(false, false, false);
  TEST_ASSERT_TRUE(g.publish(false));  // SHT31 found at prepare or at send time
  TEST_ASSERT_TRUE(g.publish(false));
}

static void test_gate_unanswered_dht11_prime_is_null() {
  olvar::ClimatePublishGate g;
  g.prepared(true, false, true);
  TEST_ASSERT_FALSE(g.publish(true));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_decodes_a_good_frame);
  RUN_TEST(test_bad_checksum_gives_null);
  RUN_TEST(test_all_zero_frame_gives_null);
  RUN_TEST(test_negative_temperature);
  RUN_TEST(test_datasheet_example_frame);
  RUN_TEST(test_two_digit_fractions);
  RUN_TEST(test_out_of_range_gives_null);
  RUN_TEST(test_pulses_to_bytes_uses_50us_threshold);
  RUN_TEST(test_floor_humidity_frame_passes_through);
  RUN_TEST(test_every_single_bit_error_is_rejected);
  RUN_TEST(test_walk_reads_a_whole_reply);
  RUN_TEST(test_walk_waits_for_the_release_to_rise);
  RUN_TEST(test_walk_line_held_low);
  RUN_TEST(test_walk_no_sensor);
  RUN_TEST(test_walk_stops_at_a_missing_bit);
  RUN_TEST(test_diagnose_reports_bytes_checksum_and_widths);
  RUN_TEST(test_format_good_frame);
  RUN_TEST(test_format_errors);
  RUN_TEST(test_format_never_truncates);
  RUN_TEST(test_throttle_allows_one_read_per_2s);
  RUN_TEST(test_throttle_survives_millis_wrap);
  RUN_TEST(test_throttle_due_at_is_absolute);
  RUN_TEST(test_throttle_settles_after_two_answered_reads);
  RUN_TEST(test_gate_nulls_dht11_first_detected_at_send_time);
  RUN_TEST(test_gate_dht11_inside_boot_probe_window_is_null);
  RUN_TEST(test_gate_sht31_always_publishes_once_prepared);
  RUN_TEST(test_gate_unanswered_dht11_prime_is_null);
  return UNITY_END();
}
