// Native tests for buildTelemetryJson (docs/PLAN.md §6 T2).
#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <string>

#include "telemetry.h"

void setUp() {}
void tearDown() {}

namespace {

// The reading/meta that produce docs/contract/telemetry.example.json.
Reading exampleReading() {
  Reading r;
  r.temperature_c = {true, 23.41f};
  r.humidity_pct = {true, 51.07f};
  r.lux = {true, 312.5f};
  r.soil_raw = {true, 2210};
  r.climate_sensor = ClimateKind::Sht31;
  return r;
}

Meta exampleMeta() {
  Meta m{};
  m.device_id = "olvar-a1b2c3";
  m.boot_id = "9f3c07ab";
  m.seq = 42;
  m.uptime_ms = 2520311;
  m.fw = "0.1.0+a1b2c3d";
  m.rssi_dbm = {true, -61};
  return m;
}

std::string build(const Reading& r, const Meta& m) {
  char out[512];
  size_t n = buildTelemetryJson(r, m, out, sizeof(out));
  TEST_ASSERT_GREATER_THAN(0, n);
  TEST_ASSERT_EQUAL(strlen(out), n);
  return std::string(out, n);
}

void parse(const std::string& json, JsonDocument& doc) {
  DeserializationError err = deserializeJson(doc, json);
  TEST_ASSERT_TRUE_MESSAGE(!err, err.c_str());
}

// test/native/test_telemetry/test_main.cpp -> docs/contract/telemetry.example.json
std::string readExampleFixture() {
  std::string path = __FILE__;
  size_t at = path.rfind("test/native/test_telemetry/");
  TEST_ASSERT_TRUE_MESSAGE(at != std::string::npos, "cannot locate repo root from __FILE__");
  path = path.substr(0, at) + "docs/contract/telemetry.example.json";
  FILE* f = fopen(path.c_str(), "rb");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, path.c_str());
  std::string s;
  char buf[256];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
  fclose(f);
  return s;
}

const char* const kKeys[] = {"device_id",    "boot_id", "seq",      "uptime_ms",      "temperature_c",
                             "humidity_pct", "lux",     "soil_raw", "climate_sensor", "rssi_dbm",
                             "fw"};

}  // namespace

static void test_example_matches_contract_fixture() {
  std::string json = build(exampleReading(), exampleMeta());
  JsonDocument got, want;
  parse(json, got);
  parse(readExampleFixture(), want);
  TEST_ASSERT_TRUE_MESSAGE(got == want, json.c_str());
  // Same key order and formatting as the fixture, byte for byte.
  std::string fixture = readExampleFixture();
  while (!fixture.empty() && (fixture.back() == '\n' || fixture.back() == '\r')) fixture.pop_back();
  TEST_ASSERT_EQUAL_STRING(fixture.c_str(), json.c_str());
}

static void test_exact_key_set_and_no_soil_pct() {
  JsonDocument doc;
  parse(build(exampleReading(), exampleMeta()), doc);
  JsonObject obj = doc.as<JsonObject>();
  TEST_ASSERT_EQUAL(11, obj.size());
  for (const char* k : kKeys) TEST_ASSERT_TRUE_MESSAGE(obj[k].is<JsonVariantConst>(), k);
  for (JsonPair kv : obj) {
    bool known = false;
    for (const char* k : kKeys) known = known || strcmp(kv.key().c_str(), k) == 0;
    TEST_ASSERT_TRUE_MESSAGE(known, kv.key().c_str());
  }
  TEST_ASSERT_FALSE(obj["soil_pct"].is<JsonVariantConst>());
}

static void test_rssi_omitted_when_not_ok() {
  Meta m = exampleMeta();
  m.rssi_dbm = {false, -61};
  std::string json = build(exampleReading(), m);
  TEST_ASSERT_NULL(strstr(json.c_str(), "rssi_dbm"));
  JsonDocument doc;
  parse(json, doc);
  TEST_ASSERT_EQUAL(10, doc.as<JsonObject>().size());
}

static void test_climate_sensor_names() {
  Reading r = exampleReading();
  r.climate_sensor = ClimateKind::Dht11;
  std::string json = build(r, exampleMeta());
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(json.c_str(), "\"soil_raw\":2210,\"climate_sensor\":\"dht11\","), json.c_str());
  r.climate_sensor = ClimateKind::Sht31;
  json = build(r, exampleMeta());
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(json.c_str(), "\"climate_sensor\":\"sht31\","), json.c_str());
  TEST_ASSERT_EQUAL_STRING("sht31", climateSensorId(ClimateKind::Sht31));
  TEST_ASSERT_EQUAL_STRING("dht11", climateSensorId(ClimateKind::Dht11));
  TEST_ASSERT_NULL(climateSensorId(ClimateKind::None));
}

static void test_climate_sensor_omitted_when_none_found() {
  Reading r = exampleReading();
  r.climate_sensor = ClimateKind::None;
  std::string json = build(r, exampleMeta());
  // Omitted, never null, so an old gateway sees exactly the pre-climate_sensor payload.
  TEST_ASSERT_NULL(strstr(json.c_str(), "climate_sensor"));
  JsonDocument doc;
  parse(json, doc);
  TEST_ASSERT_EQUAL(10, doc.as<JsonObject>().size());
}

static void test_climate_sensor_kept_when_reading_fails() {
  // The sensor is known but this read failed: values null, the sensor still named.
  Reading r;
  r.climate_sensor = ClimateKind::Dht11;
  JsonDocument doc;
  parse(build(r, exampleMeta()), doc);
  TEST_ASSERT_TRUE(doc["temperature_c"].isNull());
  TEST_ASSERT_EQUAL_STRING("dht11", doc["climate_sensor"].as<const char*>());
}

static void test_failed_fields_are_null() {
  Reading r;  // every ok=false
  std::string json = build(r, exampleMeta());
  JsonDocument doc;
  parse(json, doc);
  TEST_ASSERT_TRUE(doc["temperature_c"].isNull());
  TEST_ASSERT_TRUE(doc["humidity_pct"].isNull());
  TEST_ASSERT_TRUE(doc["lux"].isNull());
  TEST_ASSERT_TRUE(doc["soil_raw"].isNull());
  // The keys are present with an explicit null, not missing.
  TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"temperature_c\":null"));
  TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"humidity_pct\":null"));
  TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"lux\":null"));
  TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"soil_raw\":null"));
}

static void test_non_finite_values_are_null_even_when_ok() {
  Reading r = exampleReading();
  r.temperature_c = {true, NAN};
  r.humidity_pct = {true, INFINITY};
  r.lux = {true, -INFINITY};
  std::string json = build(r, exampleMeta());
  TEST_ASSERT_NULL(strstr(json.c_str(), "nan"));
  TEST_ASSERT_NULL(strstr(json.c_str(), "NaN"));
  TEST_ASSERT_NULL(strstr(json.c_str(), "inf"));
  TEST_ASSERT_NULL(strstr(json.c_str(), "Infinity"));
  JsonDocument doc;
  parse(json, doc);
  TEST_ASSERT_TRUE(doc["temperature_c"].isNull());
  TEST_ASSERT_TRUE(doc["humidity_pct"].isNull());
  TEST_ASSERT_TRUE(doc["lux"].isNull());
  TEST_ASSERT_EQUAL(2210, doc["soil_raw"].as<int>());
}

static void test_decimal_places() {
  Reading r = exampleReading();
  r.temperature_c = {true, 23.456789f};
  r.humidity_pct = {true, -0.001f};
  r.lux = {true, 312.46f};
  std::string json = build(r, exampleMeta());
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(json.c_str(), "\"temperature_c\":23.46,"), json.c_str());
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(json.c_str(), "\"humidity_pct\":0,"), json.c_str());
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(json.c_str(), "\"lux\":312.5,"), json.c_str());

  r.temperature_c = {true, -5.125f};  // exact in binary: half away from zero
  r.humidity_pct = {true, 100.0f};
  r.lux = {true, 0.04f};
  json = build(r, exampleMeta());
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(json.c_str(), "\"temperature_c\":-5.13,"), json.c_str());
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(json.c_str(), "\"humidity_pct\":100,"), json.c_str());
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(json.c_str(), "\"lux\":0,"), json.c_str());

  r.lux = {true, 65535.0f * 7};  // large but finite: plain number, no exponent
  json = build(r, exampleMeta());
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(json.c_str(), "\"lux\":458745,"), json.c_str());
}

static void test_integer_fields_full_range() {
  Meta m = exampleMeta();
  m.seq = 4294967295u;
  m.uptime_ms = 4294967295u;
  m.rssi_dbm = {true, -127};
  Reading r = exampleReading();
  r.soil_raw = {true, 0};
  std::string json = build(r, m);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(json.c_str(), "\"seq\":4294967295,"), json.c_str());
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(json.c_str(), "\"uptime_ms\":4294967295,"), json.c_str());
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(json.c_str(), "\"soil_raw\":0,"), json.c_str());
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(json.c_str(), "\"rssi_dbm\":-127,"), json.c_str());
}

static void test_longest_realistic_payload_fits_mqtt_buffer() {
  Meta m = exampleMeta();
  m.device_id = "olvar-0123456789abcdef0123456789";  // 32 chars, the longest valid id
  m.seq = 4294967295u;
  m.uptime_ms = 4294967295u;
  m.rssi_dbm = {true, -127};
  m.fw = "0.1.0-rc.10+a1b2c3d";
  Reading r = exampleReading();
  r.temperature_c = {true, -40.01f};
  r.humidity_pct = {true, 100.0f};
  r.lux = {true, 65535.9f};
  r.soil_raw = {true, 4095};
  r.climate_sensor = ClimateKind::Sht31;
  // MqttLink::payload_ is char[320].
  char out[320];
  TEST_ASSERT_GREATER_THAN(0, buildTelemetryJson(r, m, out, sizeof(out)));
}

static void test_returns_zero_when_buffer_too_small() {
  std::string full = build(exampleReading(), exampleMeta());
  char out[512];
  // Exactly enough: length + NUL.
  TEST_ASSERT_EQUAL(full.size(), buildTelemetryJson(exampleReading(), exampleMeta(), out, full.size() + 1));
  for (size_t cap : {full.size(), full.size() - 1, static_cast<size_t>(10), static_cast<size_t>(1)}) {
    memset(out, 'x', sizeof(out));
    TEST_ASSERT_EQUAL(0, buildTelemetryJson(exampleReading(), exampleMeta(), out, cap));
    TEST_ASSERT_EQUAL_CHAR('\0', out[0]);  // no truncated JSON left behind
  }
  TEST_ASSERT_EQUAL(0, buildTelemetryJson(exampleReading(), exampleMeta(), out, 0));
  TEST_ASSERT_EQUAL(0, buildTelemetryJson(exampleReading(), exampleMeta(), nullptr, 512));
}

static void test_null_meta_strings_rejected() {
  char out[512];
  Meta m = exampleMeta();
  m.fw = nullptr;
  TEST_ASSERT_EQUAL(0, buildTelemetryJson(exampleReading(), m, out, sizeof(out)));
  m = exampleMeta();
  m.device_id = nullptr;
  TEST_ASSERT_EQUAL(0, buildTelemetryJson(exampleReading(), m, out, sizeof(out)));
  m = exampleMeta();
  m.boot_id = nullptr;
  TEST_ASSERT_EQUAL(0, buildTelemetryJson(exampleReading(), m, out, sizeof(out)));
}

static void test_strings_are_escaped() {
  Meta m = exampleMeta();
  m.fw = "0.1.0+\"x\\";
  JsonDocument doc;
  parse(build(exampleReading(), m), doc);
  TEST_ASSERT_EQUAL_STRING("0.1.0+\"x\\", doc["fw"].as<const char*>());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_example_matches_contract_fixture);
  RUN_TEST(test_exact_key_set_and_no_soil_pct);
  RUN_TEST(test_rssi_omitted_when_not_ok);
  RUN_TEST(test_climate_sensor_names);
  RUN_TEST(test_climate_sensor_omitted_when_none_found);
  RUN_TEST(test_climate_sensor_kept_when_reading_fails);
  RUN_TEST(test_failed_fields_are_null);
  RUN_TEST(test_non_finite_values_are_null_even_when_ok);
  RUN_TEST(test_decimal_places);
  RUN_TEST(test_integer_fields_full_range);
  RUN_TEST(test_longest_realistic_payload_fits_mqtt_buffer);
  RUN_TEST(test_returns_zero_when_buffer_too_small);
  RUN_TEST(test_null_meta_strings_rejected);
  RUN_TEST(test_strings_are_escaped);
  return UNITY_END();
}
