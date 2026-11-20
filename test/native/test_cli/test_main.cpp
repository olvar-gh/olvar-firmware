#include <unity.h>

#include "cli.h"

void setUp() {}
void tearDown() {}

#define ASSERT_CMD(expected, line) \
  TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(expected), static_cast<int>(parseCommand(line)), line)

static void test_known_commands() {
  ASSERT_CMD(Cmd::Status, "status");
  ASSERT_CMD(Cmd::Raw, "raw");
  ASSERT_CMD(Cmd::SoilCheck, "soilcheck");
  ASSERT_CMD(Cmd::DhtWatch, "dhtwatch");
  ASSERT_CMD(Cmd::Portal, "portal");
  ASSERT_CMD(Cmd::Reboot, "reboot");
  ASSERT_CMD(Cmd::Help, "help");
}

static void test_case_insensitive() {
  ASSERT_CMD(Cmd::Status, "STATUS");
  ASSERT_CMD(Cmd::SoilCheck, "SoilCheck");
  ASSERT_CMD(Cmd::Reboot, "rEbOoT");
  ASSERT_CMD(Cmd::DhtWatch, "DhtWatch");
}

static void test_trims_whitespace() {
  ASSERT_CMD(Cmd::Status, "  status");
  ASSERT_CMD(Cmd::Status, "status  ");
  ASSERT_CMD(Cmd::Portal, "\tportal\r\n");
  ASSERT_CMD(Cmd::Help, " \r\n help \n");
}

static void test_unknown_input() {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Cmd::Unknown), static_cast<int>(parseCommand(nullptr)));
  ASSERT_CMD(Cmd::Unknown, "");
  ASSERT_CMD(Cmd::Unknown, "   \r\n");
  ASSERT_CMD(Cmd::Unknown, "stat");
  ASSERT_CMD(Cmd::Unknown, "statuss");
  ASSERT_CMD(Cmd::Unknown, "status now");
  ASSERT_CMD(Cmd::Unknown, "soil check");
  ASSERT_CMD(Cmd::Unknown, "dht watch");
  ASSERT_CMD(Cmd::Unknown, "cal");
  ASSERT_CMD(Cmd::Unknown, "r");
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_known_commands);
  RUN_TEST(test_case_insensitive);
  RUN_TEST(test_trims_whitespace);
  RUN_TEST(test_unknown_input);
  return UNITY_END();
}
