#include <string.h>
#include <unity.h>

#include "link_recovery.h"

using olvar::BrokerAddress;
using olvar::LinkRecovery;
using olvar::WifiRetry;
using Action = LinkRecovery::Action;
using Source = BrokerAddress::Source;

void setUp() {}
void tearDown() {}

static void test_wifi_retry_waits_for_a_continuous_outage() {
  WifiRetry r(0);
  TEST_ASSERT_FALSE(r.poll(WifiRetry::kDownMs - 1, false));
  TEST_ASSERT_TRUE(r.poll(WifiRetry::kDownMs, false));
  TEST_ASSERT_FALSE(r.poll(WifiRetry::kDownMs + 1, false));
  TEST_ASSERT_TRUE(r.poll(2 * WifiRetry::kDownMs, false));

  // Being up restarts the count.
  TEST_ASSERT_FALSE(r.poll(2 * WifiRetry::kDownMs + 10, true));
  TEST_ASSERT_FALSE(r.poll(3 * WifiRetry::kDownMs + 9, false));
  TEST_ASSERT_TRUE(r.poll(3 * WifiRetry::kDownMs + 10, false));
}

static void test_wifi_retry_survives_millis_wraparound() {
  const uint32_t start = 0xFFFFF000u;
  WifiRetry r(start);
  TEST_ASSERT_FALSE(r.poll(start + WifiRetry::kDownMs - 1, false));
  TEST_ASSERT_TRUE(r.poll(start + WifiRetry::kDownMs, false));
}

static void test_ladder_does_nothing_while_acks_arrive() {
  LinkRecovery l;
  for (uint32_t now = 0; now < 3 * LinkRecovery::kRestartMs; now += 60000) {
    TEST_ASSERT_EQUAL(Action::None, l.poll(now, 60000));
  }
}

static void test_ladder_resets_wifi_then_repeats_then_restarts() {
  LinkRecovery l;
  const uint32_t t0 = 5000;  // last ack
  auto at = [&](uint32_t since) { return l.poll(t0 + since, since); };
  TEST_ASSERT_EQUAL(Action::None, at(LinkRecovery::kWifiResetMs - 1));
  TEST_ASSERT_EQUAL(Action::ResetWifi, at(LinkRecovery::kWifiResetMs));
  TEST_ASSERT_EQUAL(Action::None, at(LinkRecovery::kWifiResetMs + 1));
  TEST_ASSERT_EQUAL(Action::None, at(2 * LinkRecovery::kWifiResetMs - 1));
  TEST_ASSERT_EQUAL(Action::ResetWifi, at(2 * LinkRecovery::kWifiResetMs));
  int resets = 2;
  uint32_t since = 2 * LinkRecovery::kWifiResetMs + 1000;
  for (; since < LinkRecovery::kRestartMs; since += 1000) resets += at(since) == Action::ResetWifi;
  TEST_ASSERT_EQUAL(5, resets);  // at 100, 200, 300, 400, 500 s
  TEST_ASSERT_EQUAL(Action::Restart, at(LinkRecovery::kRestartMs));
}

static void test_ladder_starts_over_after_an_ack() {
  LinkRecovery l;
  TEST_ASSERT_EQUAL(Action::ResetWifi, l.poll(LinkRecovery::kWifiResetMs, LinkRecovery::kWifiResetMs));
  // Acked shortly after the reset: the next outage gets its first reset after kWifiResetMs again.
  const uint32_t ack = LinkRecovery::kWifiResetMs + 10000;
  TEST_ASSERT_EQUAL(Action::None, l.poll(ack, 0));
  TEST_ASSERT_EQUAL(Action::None, l.poll(ack + LinkRecovery::kWifiResetMs - 1, LinkRecovery::kWifiResetMs - 1));
  TEST_ASSERT_EQUAL(Action::ResetWifi, l.poll(ack + LinkRecovery::kWifiResetMs, LinkRecovery::kWifiResetMs));
}

static void test_ladder_survives_millis_wraparound() {
  LinkRecovery l;
  const uint32_t t0 = 0xFFFF0000u;
  TEST_ASSERT_EQUAL(Action::ResetWifi, l.poll(t0 + LinkRecovery::kWifiResetMs, LinkRecovery::kWifiResetMs));
  TEST_ASSERT_EQUAL(Action::None, l.poll(t0 + 2 * LinkRecovery::kWifiResetMs - 1, 2 * LinkRecovery::kWifiResetMs - 1));
  TEST_ASSERT_EQUAL(Action::ResetWifi, l.poll(t0 + 2 * LinkRecovery::kWifiResetMs, 2 * LinkRecovery::kWifiResetMs));
}

static void test_ip_parse_and_format() {
  uint32_t ip = 0;
  TEST_ASSERT_TRUE(olvar::parseIpv4("192.168.0.42", ip));
  TEST_ASSERT_EQUAL_HEX32(olvar::ipv4(192, 168, 0, 42), ip);
  TEST_ASSERT_EQUAL_HEX32(0x2A00A8C0u, ip);  // network byte order, as lwIP and IPAddress hold it
  char buf[16];
  TEST_ASSERT_TRUE(olvar::formatIpv4(ip, buf, sizeof buf));
  TEST_ASSERT_EQUAL_STRING("192.168.0.42", buf);
  TEST_ASSERT_TRUE(olvar::formatIpv4(olvar::ipv4(255, 255, 255, 255), buf, sizeof buf));
  TEST_ASSERT_EQUAL_STRING("255.255.255.255", buf);
  TEST_ASSERT_FALSE(olvar::formatIpv4(ip, buf, 12));

  const char* bad[] = {"", "olvar-gw.local", "192.168.0", "192.168.0.42.1", "192.168.0.256",
                       "192.168..42", "1922.168.0.4", "192.168.0.42 ", " 192.168.0.42", "a.b.c.d"};
  for (const char* s : bad) TEST_ASSERT_FALSE_MESSAGE(olvar::parseIpv4(s, ip), s);
  TEST_ASSERT_FALSE(olvar::parseIpv4(nullptr, ip));
}

static void test_broker_memo_round_trip_and_host_match() {
  char memo[olvar::kBrokerMemoLen];
  const uint32_t ip = olvar::ipv4(192, 168, 0, 42);
  TEST_ASSERT_TRUE(olvar::formatBrokerMemo(ip, "olvar-gw.local", memo, sizeof memo));
  TEST_ASSERT_EQUAL_STRING("192.168.0.42 olvar-gw.local", memo);
  TEST_ASSERT_EQUAL_HEX32(ip, olvar::parseBrokerMemo(memo, "olvar-gw.local"));
  TEST_ASSERT_EQUAL_HEX32(0, olvar::parseBrokerMemo(memo, "other.local"));
  TEST_ASSERT_EQUAL_HEX32(0, olvar::parseBrokerMemo("garbage", "olvar-gw.local"));
  TEST_ASSERT_EQUAL_HEX32(0, olvar::parseBrokerMemo("1.2.3 olvar-gw.local", "olvar-gw.local"));
  TEST_ASSERT_EQUAL_HEX32(0, olvar::parseBrokerMemo(nullptr, "olvar-gw.local"));
  TEST_ASSERT_FALSE(olvar::formatBrokerMemo(0, "olvar-gw.local", memo, sizeof memo));
  TEST_ASSERT_FALSE(olvar::formatBrokerMemo(ip, "", memo, sizeof memo));
  TEST_ASSERT_FALSE(olvar::formatBrokerMemo(ip, "olvar-gw.local", memo, 10));
  // The longest mq_host (63 chars) fits.
  char host[64];
  memset(host, 'h', 63);
  host[63] = '\0';
  TEST_ASSERT_TRUE(olvar::formatBrokerMemo(olvar::ipv4(255, 255, 255, 255), host, memo, sizeof memo));
}

static void test_broker_uses_the_resolved_address_and_learns_it_on_connect() {
  BrokerAddress b;
  const uint32_t a = olvar::ipv4(192, 168, 0, 42);
  TEST_ASSERT_EQUAL_HEX32(a, b.choose(a));
  TEST_ASSERT_EQUAL(Source::Resolved, b.source());
  TEST_ASSERT_EQUAL_HEX32(0, b.known());  // not before the broker accepted it
  TEST_ASSERT_TRUE(b.connected());
  TEST_ASSERT_EQUAL_HEX32(a, b.known());
  TEST_ASSERT_EQUAL_HEX32(a, b.choose(a));
  TEST_ASSERT_FALSE(b.connected());  // unchanged: nothing to write
}

static void test_broker_falls_back_to_the_remembered_address() {
  BrokerAddress b;
  const uint32_t a = olvar::ipv4(192, 168, 0, 42);
  TEST_ASSERT_EQUAL_HEX32(0, b.choose(0));
  TEST_ASSERT_EQUAL(Source::None, b.source());
  TEST_ASSERT_FALSE(b.connected());

  b.remember(a);  // from NVS
  TEST_ASSERT_EQUAL_HEX32(a, b.choose(0));
  TEST_ASSERT_EQUAL(Source::Remembered, b.source());
  TEST_ASSERT_TRUE(b.failing());
  TEST_ASSERT_FALSE(b.connected());  // already known: no NVS write
  TEST_ASSERT_EQUAL_UINT32(2, b.failures());
  TEST_ASSERT_EQUAL_UINT32(1, b.fallbacks());

  // The Pi got a new lease: once the name resolves again, the new address is learnt.
  const uint32_t moved = olvar::ipv4(192, 168, 0, 77);
  TEST_ASSERT_EQUAL_HEX32(moved, b.choose(moved));
  TEST_ASSERT_FALSE(b.failing());
  TEST_ASSERT_TRUE(b.connected());
  TEST_ASSERT_EQUAL_HEX32(moved, b.known());
}

static void test_broker_asks_for_an_mdns_restart_every_second_failure_in_a_row() {
  BrokerAddress b;
  b.choose(0);
  TEST_ASSERT_FALSE(b.mdnsRestartDue());
  b.choose(0);
  TEST_ASSERT_TRUE(b.mdnsRestartDue());
  b.choose(0);
  TEST_ASSERT_FALSE(b.mdnsRestartDue());
  b.choose(0);
  TEST_ASSERT_TRUE(b.mdnsRestartDue());
  b.choose(olvar::ipv4(10, 0, 0, 1));
  TEST_ASSERT_FALSE(b.mdnsRestartDue());
  b.choose(0);
  TEST_ASSERT_FALSE(b.mdnsRestartDue());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_wifi_retry_waits_for_a_continuous_outage);
  RUN_TEST(test_wifi_retry_survives_millis_wraparound);
  RUN_TEST(test_ladder_does_nothing_while_acks_arrive);
  RUN_TEST(test_ladder_resets_wifi_then_repeats_then_restarts);
  RUN_TEST(test_ladder_starts_over_after_an_ack);
  RUN_TEST(test_ladder_survives_millis_wraparound);
  RUN_TEST(test_ip_parse_and_format);
  RUN_TEST(test_broker_memo_round_trip_and_host_match);
  RUN_TEST(test_broker_uses_the_resolved_address_and_learns_it_on_connect);
  RUN_TEST(test_broker_falls_back_to_the_remembered_address);
  RUN_TEST(test_broker_asks_for_an_mdns_restart_every_second_failure_in_a_row);
  return UNITY_END();
}
