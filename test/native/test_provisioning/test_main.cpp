// Native tests for the provisioning decision logic (docs/PLAN.md §6 T5).
#include <string.h>
#include <unity.h>

#include <string>

#include "settings.h"

#include "../../../src/net/provisioning.cpp"

using namespace olvar;

namespace {

Settings current() {
  Settings s;
  strcpy(s.device_id, "olvar-a1b2c3");
  strcpy(s.mqtt_host, "192.168.1.20");
  strcpy(s.mqtt_pass, "old-pass");
  return s;
}

PortalForm form(const char* id, const char* host, const char* port, const char* pass) {
  return PortalForm{id, host, port, pass};
}

void assertFormRejected(const PortalForm& f, const char* expectInMsg) {
  Settings out;
  const char* err = nullptr;
  TEST_ASSERT_FALSE(settingsFromForm(f, current(), out, &err));
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(err, expectInMsg), err);
}

}  // namespace

void setUp() {}
void tearDown() {}

static void test_ap_ssid_uses_last_three_mac_bytes() {
  const uint8_t mac[6] = {0x24, 0x6f, 0x28, 0xA1, 0x0B, 0xC3};
  char ssid[kApSsidSize];
  apSsid(mac, ssid);
  TEST_ASSERT_EQUAL_STRING("olvar-setup-a10bc3", ssid);
}

static void test_ap_password_is_10_unambiguous_chars() {
  char a[kApPasswordSize];
  char b[kApPasswordSize];
  apPasswordFromRandom(0, 0, a);
  TEST_ASSERT_EQUAL_STRING("aaaaaaaaaa", a);
  apPasswordFromRandom(0xDEADBEEF, 0x12345678, a);
  apPasswordFromRandom(0xDEADBEEF, 0x12345679, b);
  TEST_ASSERT_EQUAL_size_t(10, strlen(a));
  TEST_ASSERT_TRUE(strcmp(a, b) != 0);
  for (const char* p = a; *p; ++p) {
    TEST_ASSERT_NULL(strchr("0O1lI", *p));
    TEST_ASSERT_NOT_NULL(strchr(kPasswordAlphabet, *p));
  }
}

static void test_should_open_portal() {
  TEST_ASSERT_FALSE(shouldOpenPortal(true, false, true));
  TEST_ASSERT_TRUE(shouldOpenPortal(false, false, true));
  TEST_ASSERT_TRUE(shouldOpenPortal(true, true, true));
  TEST_ASSERT_TRUE(shouldOpenPortal(true, false, false));
}

static void test_form_accepts_and_normalises() {
  Settings out;
  const char* err = "unset";
  TEST_ASSERT_TRUE(settingsFromForm(form("  Olvar-Kitchen ", " olvar-gw.local\t", " 1884 ", "new pass "), current(), out,
                                    &err));
  TEST_ASSERT_NULL(err);
  TEST_ASSERT_EQUAL_STRING("olvar-kitchen", out.device_id);
  TEST_ASSERT_EQUAL_STRING("olvar-gw.local", out.mqtt_host);
  TEST_ASSERT_EQUAL_UINT16(1884, out.mqtt_port);
  TEST_ASSERT_EQUAL_STRING("new pass ", out.mqtt_pass);
}

static void test_form_empty_password_keeps_current() {
  Settings out;
  TEST_ASSERT_TRUE(settingsFromForm(form("olvar-a1b2c3", "10.0.0.5", "1883", ""), current(), out, nullptr));
  TEST_ASSERT_EQUAL_STRING("old-pass", out.mqtt_pass);
  TEST_ASSERT_TRUE(settingsFromForm(form("olvar-a1b2c3", "10.0.0.5", "1883", nullptr), current(), out, nullptr));
  TEST_ASSERT_EQUAL_STRING("old-pass", out.mqtt_pass);
}

static void test_form_empty_password_without_current_is_rejected() {
  Settings none;
  Settings out;
  const char* err = nullptr;
  TEST_ASSERT_FALSE(settingsFromForm(form("olvar-a1b2c3", "10.0.0.5", "1883", ""), none, out, &err));
  TEST_ASSERT_NOT_NULL(strstr(err, "password"));
}

static void test_form_rejects_bad_device_ids() {
  assertFormRejected(form("ab", "h", "1883", "p"), "device_id");
  assertFormRejected(form("-plant", "h", "1883", "p"), "device_id");
  assertFormRejected(form("olvar_plant", "h", "1883", "p"), "device_id");
  assertFormRejected(form(std::string(33, 'a').c_str(), "h", "1883", "p"), "device_id");
  assertFormRejected(form("OLVAR-GATEWAY", "h", "1883", "p"), "reserved");
}

static void test_form_rejects_bad_hosts() {
  assertFormRejected(form("olvar-a1b2c3", "  ", "1883", "p"), "MQTT host");
  assertFormRejected(form("olvar-a1b2c3", std::string(64, 'h').c_str(), "1883", "p"), "MQTT host");
  assertFormRejected(form("olvar-a1b2c3", "gw'><script>", "1883", "p"), "MQTT host");
  assertFormRejected(form("olvar-a1b2c3", "my gw", "1883", "p"), "MQTT host");
}

static void test_form_rejects_bad_ports() {
  assertFormRejected(form("olvar-a1b2c3", "h", "", "p"), "port");
  assertFormRejected(form("olvar-a1b2c3", "h", "0", "p"), "port");
  assertFormRejected(form("olvar-a1b2c3", "h", "65536", "p"), "port");
  assertFormRejected(form("olvar-a1b2c3", "h", "99999999999", "p"), "port");
  assertFormRejected(form("olvar-a1b2c3", "h", "-1", "p"), "port");
  assertFormRejected(form("olvar-a1b2c3", "h", "18 83", "p"), "port");
  Settings out;
  TEST_ASSERT_TRUE(settingsFromForm(form("olvar-a1b2c3", "h", "65535", "p"), current(), out, nullptr));
  TEST_ASSERT_EQUAL_UINT16(65535, out.mqtt_port);
}

static void test_form_rejects_long_password() {
  assertFormRejected(form("olvar-a1b2c3", "h", "1883", std::string(65, 'p').c_str()), "password");
  Settings out;
  TEST_ASSERT_TRUE(
      settingsFromForm(form("olvar-a1b2c3", "h", "1883", std::string(64, 'p').c_str()), current(), out, nullptr));
}

static void test_submit_action() {
  TEST_ASSERT_EQUAL(SubmitAction::Reject, submitAction(false, false, true));
  TEST_ASSERT_EQUAL(SubmitAction::Reject, submitAction(false, true, true));
  TEST_ASSERT_EQUAL(SubmitAction::JoinWifi, submitAction(true, true, false));
  TEST_ASSERT_EQUAL(SubmitAction::JoinWifi, submitAction(true, true, true));
  TEST_ASSERT_EQUAL(SubmitAction::SaveNow, submitAction(true, false, true));
  TEST_ASSERT_EQUAL(SubmitAction::NeedWifi, submitAction(true, false, false));
}

static void test_rejected_form_is_never_handed_to_wifimanager() {
  Settings out;
  const bool valid = settingsFromForm(form("olvar-a1b2c3", "h", "0", "p"), current(), out, nullptr);
  TEST_ASSERT_FALSE(valid);
  TEST_ASSERT_EQUAL(SubmitAction::Reject, submitAction(valid, true, true));
  TEST_ASSERT_EQUAL(SubmitAction::Reject, submitAction(valid, false, true));
}

static void test_portal_step() {
  TEST_ASSERT_EQUAL(PortalStep::Wait, portalStep(false, false, true));
  TEST_ASSERT_EQUAL(PortalStep::Save, portalStep(true, true, false));
  TEST_ASSERT_EQUAL(PortalStep::WifiFailed, portalStep(true, false, true));
  TEST_ASSERT_EQUAL(PortalStep::Reopen, portalStep(false, true, false));
}

static void test_portal_closed_without_save_restarts_at_once() {
  TEST_ASSERT_EQUAL(PortalStep::Closed, portalStep(false, false, false));
  TEST_ASSERT_TRUE(bootNormallyAfterTimeout(true, current()));
  TEST_ASSERT_FALSE(bootNormallyAfterTimeout(false, current()));
}

static void test_save_failure_after_join_reopens_portal() {
  TEST_ASSERT_EQUAL(PortalStep::Reopen, afterSaveFailed(true));
  TEST_ASSERT_EQUAL(PortalStep::Wait, afterSaveFailed(false));
}

static void test_overlong_values_are_rejected_not_shortened() {
  assertFormRejected(form("olvar-a1b2c3", "h", "100000", "p"), "port");
  assertFormRejected(form("olvar-a1b2c3", "h", "1883", std::string(65, 'p').c_str()), "password");
  assertFormRejected(form("olvar-a1b2c3", std::string(64, 'h').c_str(), "1883", "p"), "MQTT host");
}

static void test_password_with_quote_is_kept_verbatim() {
  Settings out;
  TEST_ASSERT_TRUE(settingsFromForm(form("olvar-a1b2c3", "h", "1883", "pa'ss\"<&>"), current(), out, nullptr));
  TEST_ASSERT_EQUAL_STRING("pa'ss\"<&>", out.mqtt_pass);
}

static void test_safe_for_html_attr() {
  TEST_ASSERT_TRUE(safeForHtmlAttr(""));
  TEST_ASSERT_TRUE(safeForHtmlAttr("olvar-gw.local"));
  TEST_ASSERT_FALSE(safeForHtmlAttr(nullptr));
  TEST_ASSERT_FALSE(safeForHtmlAttr("a'b"));
  TEST_ASSERT_FALSE(safeForHtmlAttr("a\"b"));
  TEST_ASSERT_FALSE(safeForHtmlAttr("<b>"));
  TEST_ASSERT_FALSE(safeForHtmlAttr("a&b"));
}

static void test_redisplay_value_never_returns_unsafe_text() {
  TEST_ASSERT_EQUAL_STRING("my-gw", redisplayValue("my-gw", "old"));
  TEST_ASSERT_EQUAL_STRING("old", redisplayValue("gw'><script>alert(1)</script>", "old"));
  TEST_ASSERT_EQUAL_STRING("", redisplayValue("x'", "y'"));
  TEST_ASSERT_EQUAL_STRING("old", redisplayValue(nullptr, "old"));
}

static void test_portal_expires_after_5_minutes_idle_across_wrap() {
  TEST_ASSERT_FALSE(portalExpired(1000, 1000 + 299999));
  TEST_ASSERT_TRUE(portalExpired(1000, 1000 + 300000));
  TEST_ASSERT_FALSE(portalExpired(0xFFFFFF00u, 0x00000100u));
  TEST_ASSERT_TRUE(portalExpired(0xFFFFFF00u, 0xFFFFFF00u + 300000u));
}

static void test_timeout_boots_normally_only_with_wifi_and_valid_settings() {
  TEST_ASSERT_TRUE(bootNormallyAfterTimeout(true, current()));
  TEST_ASSERT_FALSE(bootNormallyAfterTimeout(false, current()));
  Settings bad = current();
  bad.mqtt_host[0] = '\0';
  TEST_ASSERT_FALSE(bootNormallyAfterTimeout(true, bad));
}

namespace {

// Feeds a constant level from `from` to `to` in 10 ms steps; returns how many times it fired.
int hold(LongPressDetector& d, bool pressed, uint32_t from, uint32_t to) {
  int fired = 0;
  for (uint32_t t = from; t != to; t += 10) fired += d.update(pressed, t);
  return fired;
}

}  // namespace

static void test_long_press_fires_once_on_release() {
  LongPressDetector d;
  TEST_ASSERT_EQUAL(0, hold(d, false, 0, 100));
  TEST_ASSERT_EQUAL(0, hold(d, true, 100, 3200));
  TEST_ASSERT_EQUAL(1, hold(d, false, 3200, 3400));
  TEST_ASSERT_EQUAL(0, hold(d, false, 3400, 5000));
}

static void test_short_press_does_not_fire() {
  LongPressDetector d;
  hold(d, false, 0, 100);
  TEST_ASSERT_EQUAL(0, hold(d, true, 100, 2900));
  TEST_ASSERT_EQUAL(0, hold(d, false, 2900, 3500));
}

static void test_bounces_shorter_than_debounce_are_ignored() {
  LongPressDetector d;
  hold(d, false, 0, 100);
  hold(d, true, 100, 1500);
  TEST_ASSERT_FALSE(d.update(false, 1500));
  TEST_ASSERT_FALSE(d.update(false, 1510));
  hold(d, true, 1520, 3200);
  TEST_ASSERT_EQUAL(1, hold(d, false, 3200, 3400));
}

static void test_press_held_from_first_poll_never_fires() {
  LongPressDetector d;
  TEST_ASSERT_EQUAL(0, hold(d, true, 0, 5000));
  TEST_ASSERT_EQUAL(0, hold(d, false, 5000, 5200));
  TEST_ASSERT_EQUAL(0, hold(d, true, 5200, 8300));
  TEST_ASSERT_EQUAL(1, hold(d, false, 8300, 8500));
}

static void test_long_press_across_millis_wrap() {
  LongPressDetector d;
  const uint32_t start = 0xFFFFF000u;
  hold(d, false, start, start + 100);
  hold(d, true, start + 100, start + 3200);
  TEST_ASSERT_EQUAL(1, hold(d, false, start + 3200, start + 3400));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_ap_ssid_uses_last_three_mac_bytes);
  RUN_TEST(test_ap_password_is_10_unambiguous_chars);
  RUN_TEST(test_should_open_portal);
  RUN_TEST(test_form_accepts_and_normalises);
  RUN_TEST(test_form_empty_password_keeps_current);
  RUN_TEST(test_form_empty_password_without_current_is_rejected);
  RUN_TEST(test_form_rejects_bad_device_ids);
  RUN_TEST(test_form_rejects_bad_hosts);
  RUN_TEST(test_form_rejects_bad_ports);
  RUN_TEST(test_form_rejects_long_password);
  RUN_TEST(test_submit_action);
  RUN_TEST(test_rejected_form_is_never_handed_to_wifimanager);
  RUN_TEST(test_portal_step);
  RUN_TEST(test_portal_closed_without_save_restarts_at_once);
  RUN_TEST(test_save_failure_after_join_reopens_portal);
  RUN_TEST(test_overlong_values_are_rejected_not_shortened);
  RUN_TEST(test_password_with_quote_is_kept_verbatim);
  RUN_TEST(test_safe_for_html_attr);
  RUN_TEST(test_redisplay_value_never_returns_unsafe_text);
  RUN_TEST(test_portal_expires_after_5_minutes_idle_across_wrap);
  RUN_TEST(test_timeout_boots_normally_only_with_wifi_and_valid_settings);
  RUN_TEST(test_long_press_fires_once_on_release);
  RUN_TEST(test_short_press_does_not_fire);
  RUN_TEST(test_bounces_shorter_than_debounce_are_ignored);
  RUN_TEST(test_press_held_from_first_poll_never_fires);
  RUN_TEST(test_long_press_across_millis_wrap);
  return UNITY_END();
}
