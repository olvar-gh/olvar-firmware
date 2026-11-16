#include "cli.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

namespace {

struct Entry {
  const char* word;
  Cmd cmd;
};

constexpr Entry kCommands[] = {
    {"status", Cmd::Status}, {"raw", Cmd::Raw},       {"soilcheck", Cmd::SoilCheck},
    {"portal", Cmd::Portal}, {"reboot", Cmd::Reboot}, {"help", Cmd::Help},
};

bool isSpace(char c) { return isspace(static_cast<unsigned char>(c)) != 0; }

}  // namespace

Cmd parseCommand(const char* line) {
  if (!line) return Cmd::Unknown;
  while (isSpace(*line)) ++line;
  size_t len = strlen(line);
  while (len > 0 && isSpace(line[len - 1])) --len;

  for (const Entry& e : kCommands) {
    if (strlen(e.word) != len) continue;
    size_t i = 0;
    while (i < len && tolower(static_cast<unsigned char>(line[i])) == e.word[i]) ++i;
    if (i == len) return e.cmd;
  }
  return Cmd::Unknown;
}
