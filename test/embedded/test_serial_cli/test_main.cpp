// Drives SerialCli through an in-memory Stream on the board: `pio test -e esp32dev -f embedded/test_serial_cli`.
// Sensors may be unplugged; values then print as null.
#include <Arduino.h>
#include <unity.h>

#include "serial_cli.h"
#include "status_led.h"

using namespace olvar;

class FakeStream : public Stream {
 public:
  String in, out;
  int available() override { return in.length() - pos_; }
  int read() override { return pos_ < in.length() ? in[pos_++] : -1; }
  int peek() override { return pos_ < in.length() ? in[pos_] : -1; }
  size_t write(uint8_t c) override {
    out += static_cast<char>(c);
    return 1;
  }
  void feed(const char* s) {
    in = s;
    pos_ = 0;
    out = "";
  }

 private:
  unsigned pos_ = 0;
};

static I2cBus bus;
static ClimateSensor climate(bus);
static Bh1750Sensor bh(bus);
static SoilSensor soil;
static FakeStream io;
static int portalRequests = 0;
static int reboots = 0;

static SerialCli shell(io, climate, bh, soil,
                       CliHooks{[](CliStatus& s) {
                                  s.device_id = "olvar-a1b2c3";
                                  s.boot_id = "9f3c07ab";
                                  s.mqtt_state = "disconnected";
                                  s.seq = 42;
                                  s.skipped = 3;
                                },
                                [] { ++portalRequests; }, [] { ++reboots; }});

void setUp() {}
void tearDown() {}

static void run(const char* input) {
  io.feed(input);
  shell.poll();
  Serial.print(io.out);
}

static void contains(const char* needle) {
  TEST_ASSERT_TRUE_MESSAGE(io.out.indexOf(needle) >= 0, needle);
}

void test_help_and_unknown() {
  run("help\r\n");
  contains("soilcheck");
  run("bogus\r\n");
  contains("Unknown command 'bogus'");
  run("\r\n");
  TEST_ASSERT_EQUAL_STRING("", io.out.c_str());
}

void test_status_prints_every_field() {
  run("status\n");
  for (const char* key : {"device_id olvar-a1b2c3", "boot_id   9f3c07ab", "wifi", "mqtt      disconnected",
                          "seq       42", "skipped   3", "climate   ", "heap"}) {
    contains(key);
  }
}

void test_raw_prints_all_fields() {
  run("raw\n");
  contains("soil_raw=");
  contains("temperature_c=");
  contains("humidity_pct=");
  contains("lux=");
  contains("(temp/humidity: ");
  // With a DHT11, or no sensor at all, a second line shows the DHT11's last transaction.
  if (climate.kind() != ClimateSensor::Kind::Sht31) {
    contains("dht11 ");
    contains("start_us=");
    contains("age_ms=");
  }
}

void test_dhtwatch_streams_and_aborts() {
  run("dhtwatch\n");
  if (climate.kind() == ClimateSensor::Kind::Sht31) {
    contains("no DHT11 to watch");
    TEST_ASSERT_FALSE(shell.dhtWatchActive());
    return;
  }
  contains("DHT11 watch");
  TEST_ASSERT_TRUE(shell.dhtWatchActive());
  uint32_t t0 = millis();
  uint32_t lines = 0;
  while (millis() - t0 < 4500) {
    run("");
    if (io.out.indexOf("humidity_pct=") >= 0) ++lines;
    delay(10);
  }
  TEST_ASSERT_UINT32_WITHIN(1, 2, lines);  // every 2 s
  run("raw\n");
  contains("dhtwatch is running");
  run("q\n");
  contains("aborted");
  TEST_ASSERT_FALSE(shell.dhtWatchActive());
}

void test_portal_and_reboot_use_hooks() {
  run("portal\n");
  TEST_ASSERT_EQUAL_INT(1, portalRequests);
  TEST_ASSERT_EQUAL_INT(1, reboots);
  run("reboot\n");
  TEST_ASSERT_EQUAL_INT(1, portalRequests);
  TEST_ASSERT_EQUAL_INT(2, reboots);
}

void test_soilcheck_streams_without_blocking() {
  run("soilcheck\n");
  contains("AIR");
  TEST_ASSERT_TRUE(shell.soilCheckActive());
  uint32_t t0 = millis();
  run("\n");
  contains("Measuring AIR");
  uint32_t samples = 0;
  while (millis() - t0 < 3500) {
    uint32_t c0 = millis();
    run("");
    TEST_ASSERT_LESS_THAN_UINT32(200, millis() - c0);
    if (io.out.indexOf("soil_raw=") >= 0) ++samples;
    delay(10);
  }
  TEST_ASSERT_UINT32_WITHIN(1, 4, samples);
  run("status\n");
  contains("soilcheck is running");
  run("q\n");
  contains("aborted");
  TEST_ASSERT_FALSE(shell.soilCheckActive());
}

void test_led_drives_pin() {
  StatusLed led(pins::kLed);
  led.begin();
  led.setMode(LedMode::NoWifi);
  led.update();
  TEST_ASSERT_EQUAL_INT(HIGH, digitalRead(pins::kLed));
  led.setMode(LedMode::Online);
  led.update();
  TEST_ASSERT_EQUAL_INT(LOW, digitalRead(pins::kLed));
  led.flashPublished();
  led.update();
  TEST_ASSERT_EQUAL_INT(HIGH, digitalRead(pins::kLed));
  delay(LedPattern::kFlashMs + 10);
  led.update();
  TEST_ASSERT_EQUAL_INT(LOW, digitalRead(pins::kLed));
}

void setup() {
  Serial.begin(SerialCli::kBaud);
  delay(2000);
  bus.begin();
  climate.begin();
  soil.begin();
  UNITY_BEGIN();
  RUN_TEST(test_help_and_unknown);
  RUN_TEST(test_status_prints_every_field);
  RUN_TEST(test_raw_prints_all_fields);
  RUN_TEST(test_portal_and_reboot_use_hooks);
  RUN_TEST(test_soilcheck_streams_without_blocking);
  RUN_TEST(test_dhtwatch_streams_and_aborts);
  RUN_TEST(test_led_drives_pin);
  UNITY_END();
}

void loop() {}
