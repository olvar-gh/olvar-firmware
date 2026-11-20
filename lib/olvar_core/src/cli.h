// Serial CLI command parser (the I/O side lives in src/serial_cli.*, T7).
// Pure C++17, no Arduino.h, so it builds in native tests.
#pragma once

enum class Cmd { Status, Raw, SoilCheck, DhtWatch, Portal, Reboot, Help, Unknown };

// Case-insensitive, ignores leading/trailing whitespace (incl. "\r\n"). Anything that isn't
// exactly one known command word, including null and empty input, gives Cmd::Unknown.
Cmd parseCommand(const char* line);
