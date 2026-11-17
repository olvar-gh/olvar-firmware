#include <string.h>
#include <unity.h>

#include <map>
#include <string>

#include "ids.h"
#include "settings.h"

#include "../../../src/storage/settings_store.cpp"

using olvar::KvStore;
using olvar::SettingsStore;

namespace {

class FakeKv : public KvStore {
 public:
  std::map<std::string, std::map<std::string, std::string>> data;
  std::string ns;
  bool open = false;
  bool readOnly = false;
  bool failWrites = false;
  // Writes (puts and removes) that succeed before every later one fails, like a power cut; -1 = no limit.
  int writesLeft = -1;

  bool spendWrite() {
    if (writesLeft == 0) return false;
    if (writesLeft > 0) --writesLeft;
    return true;
  }

  bool begin(const char* n, bool ro) override {
    if (open) return false;
    if (ro && !data.count(n)) return false;
    ns = n;
    readOnly = ro;
    open = true;
    data[ns];
    return true;
  }
  void end() override { open = false; }
  bool has(const char* key) override { return open && data[ns].count(key); }
  bool getString(const char* key, char* out, size_t cap) override {
    if (!has(key)) return false;
    const std::string& v = data[ns][key];
    if (v.size() + 1 > cap) return false;
    memcpy(out, v.c_str(), v.size() + 1);
    return true;
  }
  bool put(const char* key, const std::string& v) {
    if (!open || readOnly || failWrites || !spendWrite()) return false;
    data[ns][key] = v;
    return true;
  }
  bool putString(const char* key, const char* value) override { return put(key, value); }
  uint16_t getU16(const char* key, uint16_t fallback) override {
    return has(key) ? static_cast<uint16_t>(std::stoul(data[ns][key])) : fallback;
  }
  bool putU16(const char* key, uint16_t value) override { return put(key, std::to_string(value)); }
  bool getBool(const char* key, bool fallback) override { return has(key) ? data[ns][key] == "1" : fallback; }
  bool putBool(const char* key, bool value) override { return put(key, value ? "1" : "0"); }
  bool remove(const char* key) override {
    if (!open || readOnly || !spendWrite()) return false;
    return data[ns].erase(key) == 1;
  }
  bool clear() override {
    if (!open || readOnly) return false;
    data[ns].clear();
    return true;
  }
};

const uint8_t kMac[6] = {0x24, 0x6f, 0x28, 0xa1, 0xb2, 0xc3};

Settings good() {
  Settings s;
  strcpy(s.device_id, "olvar-plant1");
  strcpy(s.mqtt_host, "192.168.1.20");
  s.mqtt_port = 1884;
  strcpy(s.mqtt_pass, "s3cret pw");
  return s;
}

void assertSame(const Settings& a, const Settings& b) {
  TEST_ASSERT_EQUAL_STRING(a.device_id, b.device_id);
  TEST_ASSERT_EQUAL_STRING(a.mqtt_host, b.mqtt_host);
  TEST_ASSERT_EQUAL_UINT16(a.mqtt_port, b.mqtt_port);
  TEST_ASSERT_EQUAL_STRING(a.mqtt_pass, b.mqtt_pass);
}

}  // namespace

void setUp() {}
void tearDown() {}

static void test_defaults_use_mac_device_id_and_port_1883() {
  Settings d = olvar::defaultSettings(kMac);
  TEST_ASSERT_EQUAL_STRING("olvar-a1b2c3", d.device_id);
  TEST_ASSERT_EQUAL_STRING("", d.mqtt_host);
  TEST_ASSERT_EQUAL_UINT16(1883, d.mqtt_port);
  TEST_ASSERT_EQUAL_STRING("", d.mqtt_pass);
}

static void test_fresh_flash_loads_defaults() {
  FakeKv kv;
  SettingsStore store(kv);
  Settings d = olvar::defaultSettings(kMac);
  Settings out = good();
  TEST_ASSERT_FALSE(store.load(out, d));
  assertSame(d, out);
  TEST_ASSERT_FALSE(store.portalRequested());
}

static void test_save_load_round_trip_with_namespace_and_keys() {
  FakeKv kv;
  SettingsStore store(kv);
  const char* err = "unset";
  TEST_ASSERT_TRUE(store.save(good(), &err));
  TEST_ASSERT_FALSE(kv.open);

  auto& ns = kv.data["olvar"];
  TEST_ASSERT_EQUAL_STRING("olvar-plant1", ns["dev_id"].c_str());
  TEST_ASSERT_EQUAL_STRING("192.168.1.20", ns["mq_host"].c_str());
  TEST_ASSERT_EQUAL_STRING("1884", ns["mq_port"].c_str());
  TEST_ASSERT_EQUAL_STRING("s3cret pw", ns["mq_pass"].c_str());
  TEST_ASSERT_EQUAL_size_t(4, ns.size());

  Settings out;
  TEST_ASSERT_TRUE(store.load(out, olvar::defaultSettings(kMac)));
  assertSame(good(), out);
  TEST_ASSERT_FALSE(kv.open);
}

static void test_missing_keys_fall_back_individually() {
  FakeKv kv;
  SettingsStore store(kv);
  TEST_ASSERT_TRUE(store.save(good()));
  kv.data["olvar"].erase("mq_port");
  kv.data["olvar"].erase("dev_id");

  Settings d = olvar::defaultSettings(kMac);
  Settings out;
  TEST_ASSERT_TRUE(store.load(out, d));
  TEST_ASSERT_EQUAL_STRING("olvar-a1b2c3", out.device_id);
  TEST_ASSERT_EQUAL_UINT16(1883, out.mqtt_port);
  TEST_ASSERT_EQUAL_STRING("192.168.1.20", out.mqtt_host);
  TEST_ASSERT_EQUAL_STRING("s3cret pw", out.mqtt_pass);
}

static void test_oversized_stored_value_falls_back() {
  FakeKv kv;
  SettingsStore store(kv);
  TEST_ASSERT_TRUE(store.save(good()));
  kv.data["olvar"]["mq_host"] = std::string(64, 'h');

  Settings d = olvar::defaultSettings(kMac);
  strcpy(d.mqtt_host, "fallback.local");
  Settings out;
  store.load(out, d);
  TEST_ASSERT_EQUAL_STRING("fallback.local", out.mqtt_host);
}

static void test_save_rejects_invalid_settings_and_writes_nothing() {
  FakeKv kv;
  SettingsStore store(kv);
  Settings s = good();
  strcpy(s.device_id, "olvar-gateway");
  const char* err = nullptr;
  TEST_ASSERT_FALSE(store.save(s, &err));
  TEST_ASSERT_NOT_NULL(strstr(err, "reserved"));
  TEST_ASSERT_EQUAL_size_t(0, kv.data.count("olvar"));
}

static void test_save_reports_write_failure() {
  FakeKv kv;
  kv.failWrites = true;
  SettingsStore store(kv);
  const char* err = nullptr;
  TEST_ASSERT_FALSE(store.save(good(), &err));
  TEST_ASSERT_EQUAL_STRING("NVS write failed", err);
  TEST_ASSERT_FALSE(kv.open);
}

Settings other() {
  Settings s;
  strcpy(s.device_id, "olvar-other");
  strcpy(s.mqtt_host, "gw.local");
  s.mqtt_port = 1883;
  strcpy(s.mqtt_pass, "new password");
  return s;
}

static void test_save_failing_partway_keeps_old_or_new_never_a_mix() {
  for (int allowed = 0; allowed < 40; ++allowed) {
    FakeKv kv;
    SettingsStore store(kv);
    TEST_ASSERT_TRUE(store.save(good()));
    kv.writesLeft = allowed;
    const char* err = nullptr;
    const bool saved = store.save(other(), &err);
    kv.writesLeft = -1;
    TEST_ASSERT_FALSE(kv.open);

    Settings out;
    TEST_ASSERT_TRUE(store.load(out, olvar::defaultSettings(kMac)));
    if (saved) {
      assertSame(other(), out);
    } else {
      TEST_ASSERT_EQUAL_STRING("NVS write failed", err);
      assertSame(good(), out);
    }

    Settings third = good();
    strcpy(third.device_id, "olvar-third");
    TEST_ASSERT_TRUE(store.save(third));
    TEST_ASSERT_TRUE(store.load(out, olvar::defaultSettings(kMac)));
    assertSame(third, out);
    TEST_ASSERT_EQUAL_size_t(4, kv.data["olvar"].size());
  }
}

static void test_save_interrupted_after_commit_survives_a_second_interruption() {
  for (int first = 0; first < 20; ++first) {
    for (int second = 0; second < 20; ++second) {
      FakeKv kv;
      SettingsStore store(kv);
      TEST_ASSERT_TRUE(store.save(good()));
      kv.writesLeft = first;
      const bool savedOther = store.save(other());
      Settings third = good();
      strcpy(third.device_id, "olvar-third");
      kv.writesLeft = second;
      const bool savedThird = store.save(third);
      kv.writesLeft = -1;

      Settings out;
      store.load(out, olvar::defaultSettings(kMac));
      assertSame(savedThird ? third : (savedOther ? other() : good()), out);
    }
  }
}

static void test_failed_first_save_on_fresh_flash_leaves_defaults() {
  for (int allowed = 0; allowed < 10; ++allowed) {
    FakeKv kv;
    SettingsStore store(kv);
    kv.writesLeft = allowed;
    const bool saved = store.save(good());
    kv.writesLeft = -1;
    Settings d = olvar::defaultSettings(kMac);
    Settings out;
    const bool any = store.load(out, d);
    TEST_ASSERT_EQUAL(saved, any);
    assertSame(saved ? good() : d, out);
  }
}

static void test_portal_flag_set_and_clear() {
  FakeKv kv;
  SettingsStore store(kv);
  TEST_ASSERT_TRUE(store.setPortalRequested(false));
  TEST_ASSERT_FALSE(store.portalRequested());
  TEST_ASSERT_TRUE(store.setPortalRequested(true));
  TEST_ASSERT_TRUE(store.portalRequested());
  TEST_ASSERT_EQUAL_STRING("1", kv.data["olvar"]["portal"].c_str());
  TEST_ASSERT_TRUE(store.setPortalRequested(false));
  TEST_ASSERT_FALSE(store.portalRequested());
  TEST_ASSERT_EQUAL_size_t(0, kv.data["olvar"].count("portal"));
}

static void test_portal_flag_does_not_disturb_settings() {
  FakeKv kv;
  SettingsStore store(kv);
  TEST_ASSERT_TRUE(store.save(good()));
  TEST_ASSERT_TRUE(store.setPortalRequested(true));
  Settings out;
  store.load(out, olvar::defaultSettings(kMac));
  assertSame(good(), out);
}

static void test_clear_erases_settings_and_flag() {
  FakeKv kv;
  SettingsStore store(kv);
  TEST_ASSERT_TRUE(store.save(good()));
  TEST_ASSERT_TRUE(store.setPortalRequested(true));
  TEST_ASSERT_TRUE(store.clear());
  TEST_ASSERT_FALSE(store.portalRequested());
  Settings d = olvar::defaultSettings(kMac);
  Settings out;
  TEST_ASSERT_FALSE(store.load(out, d));
  assertSame(d, out);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_use_mac_device_id_and_port_1883);
  RUN_TEST(test_fresh_flash_loads_defaults);
  RUN_TEST(test_save_load_round_trip_with_namespace_and_keys);
  RUN_TEST(test_missing_keys_fall_back_individually);
  RUN_TEST(test_oversized_stored_value_falls_back);
  RUN_TEST(test_save_rejects_invalid_settings_and_writes_nothing);
  RUN_TEST(test_save_reports_write_failure);
  RUN_TEST(test_save_failing_partway_keeps_old_or_new_never_a_mix);
  RUN_TEST(test_save_interrupted_after_commit_survives_a_second_interruption);
  RUN_TEST(test_failed_first_save_on_fresh_flash_leaves_defaults);
  RUN_TEST(test_portal_flag_set_and_clear);
  RUN_TEST(test_portal_flag_does_not_disturb_settings);
  RUN_TEST(test_clear_erases_settings_and_flag);
  return UNITY_END();
}
