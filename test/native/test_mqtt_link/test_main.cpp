#include <unity.h>

#include "backoff.h"
#include "../../../src/net/mqtt_link.h"

using olvar::AckLog;
using olvar::MqttSession;
using Action = MqttSession::Action;
using State = MqttSession::State;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint32_t kMid = 500;

// Mirrors MqttLink::publishTelemetry's bookkeeping: returns the seq sent, or -1 if skipped.
int64_t publish(MqttSession& s, bool clientAccepts = true) {
  if (!s.canPublish() || !clientAccepts) {
    s.skipped();
    return -1;
  }
  const uint32_t seq = s.seq();
  s.handedOff();
  return seq;
}

void bringUp(MqttSession& s, uint32_t now) {
  TEST_ASSERT_EQUAL(Action::Connect, s.poll(now, true, false, true, kMid));
  TEST_ASSERT_EQUAL(Action::PublishOnline, s.poll(now, true, true, false, kMid));
  s.onlinePublished();
  TEST_ASSERT_EQUAL(Action::None, s.poll(now, true, true, false, kMid));
}

}  // namespace

static void test_first_connect_waits_for_wifi() {
  MqttSession s(0);
  TEST_ASSERT_EQUAL(Action::None, s.poll(0, false, false, true, kMid));
  TEST_ASSERT_EQUAL(State::Waiting, s.state());
  TEST_ASSERT_EQUAL(Action::Connect, s.poll(10, true, false, true, kMid));
  TEST_ASSERT_EQUAL(State::Connecting, s.state());
  TEST_ASSERT_EQUAL(Action::None, s.poll(20, true, false, false, kMid));
}

static void test_online_is_published_on_every_connect_until_accepted() {
  MqttSession s(0);
  TEST_ASSERT_EQUAL(Action::Connect, s.poll(0, true, false, true, kMid));
  TEST_ASSERT_EQUAL(Action::PublishOnline, s.poll(1, true, true, false, kMid));
  TEST_ASSERT_EQUAL(Action::PublishOnline, s.poll(2, true, true, false, kMid));
  s.onlinePublished();
  TEST_ASSERT_EQUAL(Action::None, s.poll(3, true, true, false, kMid));

  TEST_ASSERT_EQUAL(Action::Discard, s.poll(4, true, false, true, kMid));
  TEST_ASSERT_EQUAL(Action::Connect, s.poll(4 + 1000, true, false, true, kMid));
  TEST_ASSERT_EQUAL(Action::PublishOnline, s.poll(4 + 1001, true, true, false, kMid));
}

static void test_publish_only_while_connected_and_skips_use_no_seq() {
  MqttSession s(0);
  TEST_ASSERT_EQUAL(-1, publish(s));
  TEST_ASSERT_EQUAL(-1, publish(s));
  TEST_ASSERT_EQUAL_UINT32(2, s.skippedCount());
  TEST_ASSERT_EQUAL_UINT32(0, s.seq());

  bringUp(s, 0);
  TEST_ASSERT_EQUAL(0, publish(s));
  TEST_ASSERT_EQUAL(1, publish(s));
  TEST_ASSERT_EQUAL(-1, publish(s, false));
  TEST_ASSERT_EQUAL(2, publish(s));
  TEST_ASSERT_EQUAL_UINT32(3, s.skippedCount());
}

static void test_still_connecting_is_not_publishable() {
  MqttSession s(0);
  s.poll(0, true, false, true, kMid);
  TEST_ASSERT_FALSE(s.canPublish());
  TEST_ASSERT_EQUAL(-1, publish(s));
}

static void test_message_in_flight_at_disconnect_keeps_its_seq() {
  MqttSession s(0);
  bringUp(s, 0);
  TEST_ASSERT_EQUAL(0, publish(s));
  s.acked(1);
  TEST_ASSERT_EQUAL(1, publish(s));  // never acked: in flight when the broker goes away

  TEST_ASSERT_EQUAL(Action::Discard, s.poll(100, true, false, false, kMid));
  TEST_ASSERT_FALSE(s.canPublish());

  bringUp(s, 100 + 1000);
  TEST_ASSERT_EQUAL(2, publish(s));
}

static void test_two_minute_outage_skips_intervals_without_gaps_beyond_in_flight() {
  MqttSession s(0);
  bringUp(s, 0);
  uint32_t now = 0;
  int64_t last = -1;
  for (int i = 0; i < 3; ++i, now += 60000) {
    s.poll(now, true, true, false, kMid);
    last = publish(s);
    s.acked(now + 50);
  }
  TEST_ASSERT_EQUAL(2, last);

  TEST_ASSERT_EQUAL(Action::Discard, s.poll(now, true, false, true, kMid));
  for (int i = 0; i < 2; ++i, now += 60000) {
    s.poll(now, true, false, true, kMid);
    TEST_ASSERT_EQUAL(-1, publish(s));
    if (s.state() == State::Connecting) s.poll(now + 5, true, false, true, kMid);
  }
  TEST_ASSERT_EQUAL_UINT32(2, s.skippedCount());

  while (s.state() != State::Connecting) s.poll(now += 1000, true, false, true, kMid);
  TEST_ASSERT_EQUAL(Action::PublishOnline, s.poll(now, true, true, false, kMid));
  TEST_ASSERT_EQUAL(3, publish(s));
}

static void test_failed_attempts_back_off_and_success_resets() {
  MqttSession s(0);
  TEST_ASSERT_EQUAL(Action::Connect, s.poll(0, true, false, true, kMid));
  TEST_ASSERT_EQUAL(Action::Discard, s.poll(10, true, false, true, kMid));
  TEST_ASSERT_EQUAL(Action::None, s.poll(10 + 999, true, false, true, kMid));
  TEST_ASSERT_EQUAL(Action::Connect, s.poll(10 + 1000, true, false, true, kMid));
  TEST_ASSERT_EQUAL(Action::Discard, s.poll(1020, true, false, true, kMid));
  TEST_ASSERT_EQUAL(Action::None, s.poll(1020 + 1999, true, false, true, kMid));
  TEST_ASSERT_EQUAL(Action::Connect, s.poll(1020 + 2000, true, false, true, kMid));
  s.connectFailed(3020, kMid);
  TEST_ASSERT_EQUAL(Action::None, s.poll(3020 + 3999, true, false, true, kMid));
  TEST_ASSERT_EQUAL(Action::Connect, s.poll(3020 + 4000, true, false, true, kMid));

  TEST_ASSERT_EQUAL(Action::PublishOnline, s.poll(7100, true, true, false, kMid));
  TEST_ASSERT_EQUAL(Action::Discard, s.poll(7200, true, false, false, kMid));
  TEST_ASSERT_EQUAL(Action::Connect, s.poll(7200 + 1000, true, false, true, kMid));
}

static void test_backoff_caps_at_60_s_with_jitter() {
  MqttSession s(0);
  uint32_t now = 0;
  for (int i = 0; i < 10; ++i) {
    while (s.poll(now, true, false, true, 999) != Action::Connect) now += 100;
    s.connectFailed(now, 999);
  }
  const uint32_t failedAt = now;
  while (s.poll(now, true, false, true, 999) != Action::Connect) now += 100;
  TEST_ASSERT_UINT32_WITHIN(100, 72000, now - failedAt);
}

static void test_waits_for_client_to_finish_disconnecting() {
  MqttSession s(0);
  bringUp(s, 0);
  TEST_ASSERT_EQUAL(Action::Discard, s.poll(10, true, false, false, kMid));
  TEST_ASSERT_EQUAL(Action::None, s.poll(5000, true, false, false, kMid));
  TEST_ASSERT_EQUAL(Action::Connect, s.poll(5001, true, false, true, kMid));
}

static void test_no_reconnect_while_wifi_down() {
  MqttSession s(0);
  bringUp(s, 0);
  TEST_ASSERT_EQUAL(Action::Discard, s.poll(10, false, false, true, kMid));
  TEST_ASSERT_EQUAL(Action::None, s.poll(50000, false, false, true, kMid));
  TEST_ASSERT_EQUAL(Action::Connect, s.poll(50001, true, false, true, kMid));
}

static void test_stuck_connect_is_aborted_once() {
  MqttSession s(0);
  s.poll(0, true, false, true, kMid);
  TEST_ASSERT_EQUAL(Action::None, s.poll(MqttSession::kConnectTimeoutMs - 1, true, false, false, kMid));
  TEST_ASSERT_EQUAL(Action::Abort, s.poll(MqttSession::kConnectTimeoutMs, true, false, false, kMid));
  TEST_ASSERT_EQUAL(Action::None, s.poll(MqttSession::kConnectTimeoutMs + 10, true, false, false, kMid));
  TEST_ASSERT_EQUAL(Action::Discard, s.poll(MqttSession::kConnectTimeoutMs + 20, true, false, true, kMid));
}

static void test_supervisor_restarts_after_10_min_without_ack() {
  MqttSession s(0);
  bringUp(s, 0);
  TEST_ASSERT_EQUAL(Action::None, s.poll(MqttSession::kSupervisorMs - 1, true, true, false, kMid));
  s.acked(MqttSession::kSupervisorMs - 1);
  TEST_ASSERT_EQUAL(Action::None, s.poll(2 * MqttSession::kSupervisorMs - 2, true, true, false, kMid));
  TEST_ASSERT_EQUAL(Action::Restart, s.poll(2 * MqttSession::kSupervisorMs - 1, true, true, false, kMid));

  MqttSession never(0);
  TEST_ASSERT_EQUAL(Action::Restart, never.poll(MqttSession::kSupervisorMs, false, false, true, kMid));
}

static void test_timers_survive_millis_wraparound() {
  const uint32_t start = 0xFFFFFF00u;
  MqttSession s(start);
  bringUp(s, start);
  TEST_ASSERT_EQUAL(Action::Discard, s.poll(start + 10, true, false, true, kMid));
  TEST_ASSERT_EQUAL(Action::None, s.poll(start + 500, true, false, true, kMid));
  TEST_ASSERT_EQUAL(Action::Connect, s.poll(start + 1010, true, false, true, kMid));
  TEST_ASSERT_EQUAL(Action::PublishOnline, s.poll(start + 1020, true, true, false, kMid));
  TEST_ASSERT_EQUAL(Action::PublishOnline, s.poll(start + MqttSession::kSupervisorMs - 1, true, true, false, kMid));
  TEST_ASSERT_EQUAL(Action::Restart, s.poll(start + MqttSession::kSupervisorMs, true, true, false, kMid));
}

static void test_wifi_loss_ends_a_session_the_client_still_reports_connected() {
  MqttSession s(0);
  bringUp(s, 0);
  TEST_ASSERT_EQUAL(0, publish(s));
  TEST_ASSERT_EQUAL(Action::Discard, s.poll(100, false, true, false, kMid));
  TEST_ASSERT_FALSE(s.canPublish());
  TEST_ASSERT_EQUAL(-1, publish(s));
  TEST_ASSERT_EQUAL_UINT32(1, s.skippedCount());

  TEST_ASSERT_EQUAL(Action::None, s.poll(5000, true, true, false, kMid));
  TEST_ASSERT_EQUAL(Action::None, s.poll(5001, true, false, false, kMid));
  TEST_ASSERT_EQUAL(Action::Connect, s.poll(5002, true, false, true, kMid));
  TEST_ASSERT_EQUAL(Action::PublishOnline, s.poll(5003, true, true, false, kMid));
  TEST_ASSERT_EQUAL(1, publish(s));
}

static void test_wifi_loss_while_connecting_drops_the_attempt() {
  MqttSession s(0);
  s.poll(0, true, false, true, kMid);
  TEST_ASSERT_EQUAL(Action::Discard, s.poll(10, false, false, false, kMid));
  TEST_ASSERT_EQUAL(State::Waiting, s.state());
  TEST_ASSERT_EQUAL(Action::None, s.poll(20, false, false, true, kMid));
}

static void test_ack_recorded_before_id_is_known_is_kept() {
  AckLog log;
  log.record(7);
  TEST_ASSERT_TRUE(log.take(7));
  TEST_ASSERT_FALSE(log.take(7));
}

static void test_ack_log_ignores_unknown_and_zero_ids() {
  AckLog log;
  log.record(0);
  log.record(3);
  TEST_ASSERT_FALSE(log.take(0));
  TEST_ASSERT_FALSE(log.take(4));
  TEST_ASSERT_TRUE(log.take(3));
}

static void test_ack_log_keeps_the_last_slots_and_clears() {
  AckLog log;
  for (uint16_t id = 1; id <= AckLog::kSlots + 2; ++id) log.record(id);
  TEST_ASSERT_FALSE(log.take(1));
  TEST_ASSERT_FALSE(log.take(2));
  TEST_ASSERT_TRUE(log.take(3));
  TEST_ASSERT_TRUE(log.take(AckLog::kSlots + 2));
  log.clear();
  TEST_ASSERT_FALSE(log.take(4));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_first_connect_waits_for_wifi);
  RUN_TEST(test_online_is_published_on_every_connect_until_accepted);
  RUN_TEST(test_publish_only_while_connected_and_skips_use_no_seq);
  RUN_TEST(test_still_connecting_is_not_publishable);
  RUN_TEST(test_message_in_flight_at_disconnect_keeps_its_seq);
  RUN_TEST(test_two_minute_outage_skips_intervals_without_gaps_beyond_in_flight);
  RUN_TEST(test_failed_attempts_back_off_and_success_resets);
  RUN_TEST(test_backoff_caps_at_60_s_with_jitter);
  RUN_TEST(test_waits_for_client_to_finish_disconnecting);
  RUN_TEST(test_no_reconnect_while_wifi_down);
  RUN_TEST(test_stuck_connect_is_aborted_once);
  RUN_TEST(test_supervisor_restarts_after_10_min_without_ack);
  RUN_TEST(test_timers_survive_millis_wraparound);
  RUN_TEST(test_wifi_loss_ends_a_session_the_client_still_reports_connected);
  RUN_TEST(test_wifi_loss_while_connecting_drops_the_attempt);
  RUN_TEST(test_ack_recorded_before_id_is_known_is_kept);
  RUN_TEST(test_ack_log_ignores_unknown_and_zero_ids);
  RUN_TEST(test_ack_log_keeps_the_last_slots_and_clears);
  return UNITY_END();
}
