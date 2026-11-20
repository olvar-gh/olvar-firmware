#include <string.h>
#include <unity.h>

#include "link_record.h"

using olvar::LinkRecord;
using olvar::LinkRecordNames;
using olvar::RestartCause;

void setUp() {}
void tearDown() {}

namespace {

const LinkRecordNames kNames{"SW (software restart)", "CONNECTED", "BEACON_TIMEOUT"};

}  // namespace

static void test_fresh_record_is_valid_and_empty() {
  LinkRecord r = olvar::newBootRecord(nullptr, 1);
  TEST_ASSERT_TRUE(olvar::linkRecordValid(r));
  TEST_ASSERT_EQUAL_UINT8(1, r.resetReason);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RestartCause::None), r.cause);
  TEST_ASSERT_EQUAL_UINT8(olvar::kNoReason, r.wifiReason);
  TEST_ASSERT_EQUAL_UINT8(olvar::kNoReason, r.mqttReason);
  TEST_ASSERT_EQUAL_UINT32(0, r.restartChain);
  TEST_ASSERT_EQUAL_UINT32(0, r.mqttConnects);
}

static void test_any_change_without_reseal_invalidates() {
  LinkRecord r = olvar::newBootRecord(nullptr, 3);
  r.wifiDrops = 2;
  TEST_ASSERT_FALSE(olvar::linkRecordValid(r));
  olvar::sealLinkRecord(r);
  TEST_ASSERT_TRUE(olvar::linkRecordValid(r));
  r.magic ^= 1;
  olvar::sealLinkRecord(r);
  TEST_ASSERT_FALSE(olvar::linkRecordValid(r));  // RTC garbage after power-up

  LinkRecord zero;
  memset(&zero, 0, sizeof zero);
  TEST_ASSERT_FALSE(olvar::linkRecordValid(zero));
  LinkRecord ones;
  memset(&ones, 0xFF, sizeof ones);
  TEST_ASSERT_FALSE(olvar::linkRecordValid(ones));
}

static void test_supervisor_restarts_in_a_row_are_counted() {
  LinkRecord prev = olvar::newBootRecord(nullptr, 1);
  prev.cause = static_cast<uint8_t>(RestartCause::Supervisor);
  LinkRecord next = olvar::newBootRecord(&prev, 3);
  TEST_ASSERT_EQUAL_UINT32(1, next.restartChain);
  next.cause = static_cast<uint8_t>(RestartCause::Supervisor);
  LinkRecord third = olvar::newBootRecord(&next, 3);
  TEST_ASSERT_EQUAL_UINT32(2, third.restartChain);
  // Any other ending breaks the chain.
  third.cause = static_cast<uint8_t>(RestartCause::Command);
  TEST_ASSERT_EQUAL_UINT32(0, olvar::newBootRecord(&third, 3).restartChain);
  third.cause = static_cast<uint8_t>(RestartCause::None);
  TEST_ASSERT_EQUAL_UINT32(0, olvar::newBootRecord(&third, 4).restartChain);
}

static void test_format_names_every_field() {
  LinkRecord r = olvar::newBootRecord(nullptr, 3);
  r.cause = static_cast<uint8_t>(RestartCause::Supervisor);
  r.wifiStatus = 3;
  r.rssi = -71;
  r.wifiReason = 200;
  r.mqttReason = 7;
  r.resolveFailing = 1;
  r.uptimeS = 3600;
  r.sinceAckS = 600;
  r.wifiDrops = 4;
  r.wifiRetries = 1;
  r.wifiResets = 5;
  r.mqttConnects = 6;
  r.mqttDrops = 5;
  r.ackTimeouts = 2;
  r.resolveFails = 9;
  r.fallbacks = 8;
  r.restartChain = 3;
  char out[700];
  TEST_ASSERT_TRUE(olvar::formatLinkRecord(r, kNames, "  ", out, sizeof out));
  const char* want[] = {
      "  reset    SW (software restart); ended by: supervisor restart (no acknowledged telemetry)\n",
      "  uptime   3600 s, 600 s since the last acked telemetry, 3 supervisor restart(s) in a row before\n",
      "  wifi     CONNECTED, rssi -71 dBm; 4 drop(s), last reason 200 BEACON_TIMEOUT; 1 retry(s), 5 forced reset(s)\n",
      "  mqtt     6 connect(s), 5 drop(s), 2 ack timeout(s), last reason TCP_DISCONNECTED\n",
      "  broker   name lookup FAILING; 9 failed lookup(s), 8 fallback(s) to the remembered address\n",
  };
  for (const char* line : want) TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, line), line);
}

static void test_format_without_reasons_and_when_cut_short() {
  LinkRecord r = olvar::newBootRecord(nullptr, 1);
  char out[700];
  TEST_ASSERT_TRUE(olvar::formatLinkRecord(r, kNames, "", out, sizeof out));
  TEST_ASSERT_NOT_NULL(strstr(out, "last reason none;"));
  TEST_ASSERT_NOT_NULL(strstr(out, "last reason none\n"));
  TEST_ASSERT_NOT_NULL(strstr(out, "name lookup ok;"));
  TEST_ASSERT_NOT_NULL(strstr(out, "ended by: not restarted by the firmware"));

  char tiny[40];
  TEST_ASSERT_FALSE(olvar::formatLinkRecord(r, kNames, "", tiny, sizeof tiny));
  TEST_ASSERT_TRUE(strlen(tiny) < sizeof tiny);
}

static void test_reason_names() {
  TEST_ASSERT_EQUAL_STRING("MQTT_NOT_AUTHORIZED", olvar::mqttReasonName(5));
  TEST_ASSERT_EQUAL_STRING("none", olvar::mqttReasonName(olvar::kNoReason));
  TEST_ASSERT_EQUAL_STRING("unknown", olvar::mqttReasonName(42));
  TEST_ASSERT_EQUAL_STRING("reboot command", olvar::restartCauseName(2));
  TEST_ASSERT_EQUAL_STRING("unknown", olvar::restartCauseName(9));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_fresh_record_is_valid_and_empty);
  RUN_TEST(test_any_change_without_reseal_invalidates);
  RUN_TEST(test_supervisor_restarts_in_a_row_are_counted);
  RUN_TEST(test_format_names_every_field);
  RUN_TEST(test_format_without_reasons_and_when_cut_short);
  RUN_TEST(test_reason_names);
  return UNITY_END();
}
