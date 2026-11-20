#include "link_recovery.h"

#include <stdio.h>
#include <string.h>

namespace olvar {

uint32_t ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  return static_cast<uint32_t>(a) | static_cast<uint32_t>(b) << 8 | static_cast<uint32_t>(c) << 16 |
         static_cast<uint32_t>(d) << 24;
}

bool parseIpv4(const char* s, uint32_t& out) {
  if (s == nullptr) return false;
  uint8_t octets[4];
  for (int i = 0; i < 4; ++i) {
    if (i > 0 && *s++ != '.') return false;
    if (*s < '0' || *s > '9') return false;
    uint32_t v = 0;
    int digits = 0;
    while (*s >= '0' && *s <= '9') {
      v = v * 10 + static_cast<uint32_t>(*s++ - '0');
      if (++digits > 3 || v > 255) return false;
    }
    octets[i] = static_cast<uint8_t>(v);
  }
  if (*s != '\0') return false;
  out = ipv4(octets[0], octets[1], octets[2], octets[3]);
  return true;
}

bool formatIpv4(uint32_t ip, char* out, size_t cap) {
  const int n = snprintf(out, cap, "%u.%u.%u.%u", static_cast<unsigned>(ip & 0xFF),
                         static_cast<unsigned>(ip >> 8 & 0xFF), static_cast<unsigned>(ip >> 16 & 0xFF),
                         static_cast<unsigned>(ip >> 24 & 0xFF));
  return n > 0 && static_cast<size_t>(n) < cap;
}

bool formatBrokerMemo(uint32_t ip, const char* host, char* out, size_t cap) {
  char a[16];
  if (ip == 0 || host == nullptr || *host == '\0' || !formatIpv4(ip, a, sizeof a)) return false;
  const int n = snprintf(out, cap, "%s %s", a, host);
  return n > 0 && static_cast<size_t>(n) < cap;
}

uint32_t parseBrokerMemo(const char* memo, const char* host) {
  if (memo == nullptr || host == nullptr) return 0;
  const char* space = strchr(memo, ' ');
  if (space == nullptr || static_cast<size_t>(space - memo) >= 16) return 0;
  if (strcmp(space + 1, host) != 0) return 0;
  char a[16];
  memcpy(a, memo, space - memo);
  a[space - memo] = '\0';
  uint32_t ip = 0;
  return parseIpv4(a, ip) ? ip : 0;
}

}  // namespace olvar
