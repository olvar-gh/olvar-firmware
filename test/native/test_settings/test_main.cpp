#include <string.h>
#include <unity.h>

#include "settings.h"

void setUp() {}
void tearDown() {}

static Settings good() {
  Settings s;
  strcpy(s.device_id, "olvar-a1b2c3");
  strcpy(s.mqtt_host, "olvar-gw.local");
  strcpy(s.mqtt_pass, "pw");
  return s;
}

static void assertRejected(const Settings& s, const char* expectInMsg) {
  const char* err = nullptr;
  TEST_ASSERT_FALSE(validate(s, &err));
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(err, expectInMsg), err);
}

static void setId(Settings& s, const char* id) { strcpy(s.device_id, id); }

static void test_defaults() {
  Settings s;
  TEST_ASSERT_EQUAL_UINT16(1883, s.mqtt_port);
  TEST_ASSERT_EQUAL_STRING("", s.device_id);
  TEST_ASSERT_EQUAL_STRING("", s.mqtt_host);
  TEST_ASSERT_EQUAL_STRING("", s.mqtt_pass);
  TEST_ASSERT_FALSE(validate(s, nullptr));
}

static void test_valid_settings_pass() {
  const char* err = "unset";
  TEST_ASSERT_TRUE(validate(good(), &err));
  TEST_ASSERT_NULL(err);
  TEST_ASSERT_TRUE(validate(good(), nullptr));
}

static void test_accepts_edge_device_ids() {
  const char* ok[] = {"abc", "0ab", "a--", "olvar-gateway2", "xolvar-gateway",
                      "abcdefghijklmnopqrstuvwxyz012345"};  // 32 chars
  for (const char* id : ok) {
    Settings s = good();
    setId(s, id);
    TEST_ASSERT_TRUE_MESSAGE(validate(s, nullptr), id);
  }
}

static void test_rejects_bad_device_ids() {
  const char* bad[] = {"", "a", "ab", "-plant", "Olvar-a1", "olvar_a1", "olvar/a1",
                       "olvar+a1", "olvar#a1", "olvar a1"};
  for (const char* id : bad) {
    Settings s = good();
    setId(s, id);
    assertRejected(s, "device_id must be 3-32 chars");
  }
}

static void test_rejects_33_char_device_id_without_terminator() {
  Settings s = good();
  memset(s.device_id, 'a', sizeof(s.device_id));  // 33 chars, no NUL
  assertRejected(s, "device_id must be 3-32 chars");
}

static void test_rejects_reserved_gateway_id() {
  Settings s = good();
  setId(s, "olvar-gateway");
  assertRejected(s, "reserved");
}

static void test_rejects_empty_host() {
  Settings s = good();
  s.mqtt_host[0] = '\0';
  assertRejected(s, "MQTT host must not be empty");
}

static void test_rejects_unterminated_host() {
  Settings s = good();
  memset(s.mqtt_host, 'h', sizeof(s.mqtt_host));
  assertRejected(s, "MQTT host");
}

static void test_rejects_port_zero() {
  Settings s = good();
  s.mqtt_port = 0;
  assertRejected(s, "MQTT port");
  s.mqtt_port = 65535;
  TEST_ASSERT_TRUE(validate(s, nullptr));
}

static void test_rejects_empty_password() {
  Settings s = good();
  s.mqtt_pass[0] = '\0';
  assertRejected(s, "MQTT password must not be empty");
}

static void test_accepts_64_char_password_rejects_unterminated() {
  Settings s = good();
  memset(s.mqtt_pass, 'p', 64);
  s.mqtt_pass[64] = '\0';
  TEST_ASSERT_TRUE(validate(s, nullptr));
  memset(s.mqtt_pass, 'p', sizeof(s.mqtt_pass));
  assertRejected(s, "MQTT password");
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_defaults);
  RUN_TEST(test_valid_settings_pass);
  RUN_TEST(test_accepts_edge_device_ids);
  RUN_TEST(test_rejects_bad_device_ids);
  RUN_TEST(test_rejects_33_char_device_id_without_terminator);
  RUN_TEST(test_rejects_reserved_gateway_id);
  RUN_TEST(test_rejects_empty_host);
  RUN_TEST(test_rejects_unterminated_host);
  RUN_TEST(test_rejects_port_zero);
  RUN_TEST(test_rejects_empty_password);
  RUN_TEST(test_accepts_64_char_password_rejects_unterminated);
  return UNITY_END();
}
