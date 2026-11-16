// Native tests for ids.h: device id rule, MAC/boot ids, topics (docs/PLAN.md §6 T2).
#include <string.h>
#include <unity.h>

#include <string>

#include "ids.h"

void setUp() {}
void tearDown() {}

static void test_valid_device_ids() {
  TEST_ASSERT_TRUE(isValidDeviceId("abc"));
  TEST_ASSERT_TRUE(isValidDeviceId("olvar-a1b2c3"));
  TEST_ASSERT_TRUE(isValidDeviceId("0ab"));
  TEST_ASSERT_TRUE(isValidDeviceId("a--"));
  TEST_ASSERT_TRUE(isValidDeviceId("olvar-gateway2"));
  TEST_ASSERT_TRUE(isValidDeviceId("olvar-gatewa"));
  TEST_ASSERT_TRUE(isValidDeviceId(std::string(32, 'a').c_str()));
  TEST_ASSERT_TRUE(isValidDeviceId(("a" + std::string(31, '-')).c_str()));
}

static void test_invalid_device_ids() {
  TEST_ASSERT_FALSE(isValidDeviceId(nullptr));
  TEST_ASSERT_FALSE(isValidDeviceId(""));
  TEST_ASSERT_FALSE(isValidDeviceId("a"));
  TEST_ASSERT_FALSE(isValidDeviceId("ab"));
  TEST_ASSERT_FALSE(isValidDeviceId("-plant"));
  TEST_ASSERT_FALSE(isValidDeviceId("olvar-gateway"));
  TEST_ASSERT_FALSE(isValidDeviceId(std::string(33, 'a').c_str()));
  TEST_ASSERT_FALSE(isValidDeviceId(std::string(100, 'a').c_str()));
  TEST_ASSERT_FALSE(isValidDeviceId("ab/c"));
  TEST_ASSERT_FALSE(isValidDeviceId("ab+c"));
  TEST_ASSERT_FALSE(isValidDeviceId("ab#c"));
  TEST_ASSERT_FALSE(isValidDeviceId("Olvar-a1b2c3"));
  TEST_ASSERT_FALSE(isValidDeviceId("olvar-A1B2C3"));
  TEST_ASSERT_FALSE(isValidDeviceId("ab_c"));
  TEST_ASSERT_FALSE(isValidDeviceId("ab c"));
  TEST_ASSERT_FALSE(isValidDeviceId("abc\n"));
  TEST_ASSERT_FALSE(isValidDeviceId("ab.c"));
  TEST_ASSERT_FALSE(isValidDeviceId("abc\xc3\xa9"));
}

static void test_device_id_from_mac() {
  const uint8_t mac[6] = {0x24, 0x6f, 0x28, 0xa1, 0xb2, 0xc3};
  char id[13];
  deviceIdFromMac(mac, id);
  TEST_ASSERT_EQUAL_STRING("olvar-a1b2c3", id);
  TEST_ASSERT_TRUE(isValidDeviceId(id));

  const uint8_t zero[6] = {0xff, 0xff, 0xff, 0x00, 0x0a, 0xf0};
  deviceIdFromMac(zero, id);
  TEST_ASSERT_EQUAL_STRING("olvar-000af0", id);

  // Every possible last byte (and a spread of the others) yields a valid id.
  uint8_t m[6] = {0, 0, 0, 0, 0, 0};
  for (int a = 0; a < 256; a += 17) {
    for (int b = 0; b < 256; ++b) {
      m[3] = static_cast<uint8_t>(a);
      m[4] = static_cast<uint8_t>(255 - b);
      m[5] = static_cast<uint8_t>(b);
      deviceIdFromMac(m, id);
      TEST_ASSERT_EQUAL(12, strlen(id));
      TEST_ASSERT_TRUE_MESSAGE(isValidDeviceId(id), id);
    }
  }
}

static void test_boot_id_from_random() {
  char id[9];
  bootIdFromRandom(0x9f3c07abu, id);
  TEST_ASSERT_EQUAL_STRING("9f3c07ab", id);
  bootIdFromRandom(0, id);
  TEST_ASSERT_EQUAL_STRING("00000000", id);
  bootIdFromRandom(0x1u, id);
  TEST_ASSERT_EQUAL_STRING("00000001", id);
  bootIdFromRandom(0xffffffffu, id);
  TEST_ASSERT_EQUAL_STRING("ffffffff", id);
  bootIdFromRandom(0xABCDEF12u, id);
  TEST_ASSERT_EQUAL_STRING("abcdef12", id);
  for (uint32_t r = 1; r != 0; r *= 3) {
    bootIdFromRandom(r, id);
    TEST_ASSERT_EQUAL(8, strlen(id));
    for (int i = 0; i < 8; ++i) {
      TEST_ASSERT_TRUE((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f'));
    }
    if (r > 0x55555555u) break;
  }
}

static void test_topics() {
  char out[64];
  TEST_ASSERT_EQUAL(31, telemetryTopic("olvar-a1b2c3", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("olvar/v1/olvar-a1b2c3/telemetry", out);
  TEST_ASSERT_EQUAL(28, statusTopic("olvar-a1b2c3", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("olvar/v1/olvar-a1b2c3/status", out);
}

static void test_topics_overflow_and_null() {
  char out[64];
  // "olvar/v1/abc/status" is 19 chars: needs cap 20.
  TEST_ASSERT_EQUAL(19, statusTopic("abc", out, 20));
  memset(out, 'x', sizeof(out));
  TEST_ASSERT_EQUAL(0, statusTopic("abc", out, 19));
  TEST_ASSERT_EQUAL_CHAR('\0', out[0]);
  TEST_ASSERT_EQUAL(0, telemetryTopic("abc", out, 5));
  TEST_ASSERT_EQUAL(0, telemetryTopic("abc", out, 0));
  TEST_ASSERT_EQUAL(0, telemetryTopic(nullptr, out, sizeof(out)));
  TEST_ASSERT_EQUAL(0, statusTopic("abc", nullptr, sizeof(out)));
  // A max-length id fits the topic buffer size the adapters should use.
  std::string id(32, 'z');
  char big[9 + 32 + 10 + 1];
  TEST_ASSERT_EQUAL(sizeof(big) - 1, telemetryTopic(id.c_str(), big, sizeof(big)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_valid_device_ids);
  RUN_TEST(test_invalid_device_ids);
  RUN_TEST(test_device_id_from_mac);
  RUN_TEST(test_boot_id_from_random);
  RUN_TEST(test_topics);
  RUN_TEST(test_topics_overflow_and_null);
  return UNITY_END();
}
