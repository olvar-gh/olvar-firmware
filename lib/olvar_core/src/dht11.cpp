#include "dht11.h"

#include <stdio.h>

namespace olvar {

namespace {

// The digits of n placed after the decimal point: 1 -> 0.1, 12 -> 0.12, 0 -> 0.
float fraction(uint8_t n) {
  if (n < 10) return n / 10.0f;
  if (n < 100) return n / 100.0f;
  return n / 1000.0f;
}

}  // namespace

void dht11PulsesToBytes(const uint16_t highUs[kDht11Bits], uint8_t bytes[5]) {
  for (size_t i = 0; i < 5; ++i) bytes[i] = 0;
  for (size_t i = 0; i < kDht11Bits; ++i) {
    bytes[i / 8] = static_cast<uint8_t>(bytes[i / 8] << 1);
    if (highUs[i] >= kDht11OneMinUs) bytes[i / 8] |= 1;
  }
}

Dht11Diag dht11Diagnose(const Dht11Pulses& p) {
  Dht11Diag d;
  d.stage = p.stage;
  d.bits = p.bits;
  for (size_t i = 0; i < p.bits && i < kDht11Bits; ++i) {
    uint16_t w = p.highUs[i];
    if (w >= kDht11OneMinUs) {
      if (d.ones == 0 || w < d.oneMinUs) d.oneMinUs = w;
      if (d.ones == 0 || w > d.oneMaxUs) d.oneMaxUs = w;
      ++d.ones;
    } else {
      if (d.zeros == 0 || w < d.zeroMinUs) d.zeroMinUs = w;
      if (d.zeros == 0 || w > d.zeroMaxUs) d.zeroMaxUs = w;
      ++d.zeros;
    }
  }
  if (p.stage == Dht11Stage::Done) {
    dht11PulsesToBytes(p.highUs, d.bytes);
    d.checksumOk = static_cast<uint8_t>(d.bytes[0] + d.bytes[1] + d.bytes[2] + d.bytes[3]) == d.bytes[4];
  }
  return d;
}

size_t formatDht11Diag(const Dht11Diag& d, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = '\0';
  char bit0[16] = "none", bit1[16] = "none";
  if (d.zeros) snprintf(bit0, sizeof(bit0), "%u..%u", unsigned(d.zeroMinUs), unsigned(d.zeroMaxUs));
  if (d.ones) snprintf(bit1, sizeof(bit1), "%u..%u", unsigned(d.oneMinUs), unsigned(d.oneMaxUs));
  int n;
  if (d.stage == Dht11Stage::Done) {
    n = snprintf(out, cap, "bytes=%02X %02X %02X %02X %02X checksum=%s bit0_us=%s bit1_us=%s start_us=%lu",
                 d.bytes[0], d.bytes[1], d.bytes[2], d.bytes[3], d.bytes[4], d.checksumOk ? "ok" : "bad",
                 bit0, bit1, static_cast<unsigned long>(d.startLowUs));
  } else {
    const char* error = "";
    switch (d.stage) {
      case Dht11Stage::Release: error = "line_low"; break;
      case Dht11Stage::NoResponse: error = "no_response"; break;
      case Dht11Stage::Response: error = "response_cut"; break;
      case Dht11Stage::Data: error = "bit_timeout"; break;
      case Dht11Stage::Done: break;
    }
    n = snprintf(out, cap, "error=%s bits=%u bit0_us=%s bit1_us=%s start_us=%lu", error,
                 unsigned(d.bits), bit0, bit1, static_cast<unsigned long>(d.startLowUs));
  }
  if (n < 0 || static_cast<size_t>(n) >= cap) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

TempHumidity dht11Decode(const uint8_t bytes[5]) {
  TempHumidity r;
  uint8_t sum = static_cast<uint8_t>(bytes[0] + bytes[1] + bytes[2] + bytes[3]);
  if (sum != bytes[4]) return r;
  if ((bytes[0] | bytes[1] | bytes[2] | bytes[3]) == 0) return r;  // line held low, not a reading
  float h = bytes[0] + fraction(bytes[1]);
  float t = bytes[2] + fraction(bytes[3] & 0x7F);
  if (bytes[3] & 0x80) t = -t;
  if (h > 100 || t < -40 || t > 80) return r;  // garbage filter, wider than the -20..60 C rating
  r.temperature_c = {true, t};
  r.humidity_pct = {true, h};
  return r;
}

}  // namespace olvar
