// DHT11 frame decoding and the 2 s read pacing (lib/olvar_core/src/dht11.*).
#include <unity.h>

#include "dht11.h"

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
