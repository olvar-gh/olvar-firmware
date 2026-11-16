#include "telemetry.h"

#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

// Longest %.Nf of a finite float (~3.4e38) is 39 digits + sign + "." + 2 dp.
constexpr size_t kNumBuf = 48;

// Formats v with at most `decimals` decimal places (half away from zero),
// trailing zeros trimmed, and never "-0". Writes "" for non-finite values.
void formatFixed(float v, int decimals, char (&out)[kNumBuf]) {
  out[0] = '\0';
  if (!isfinite(v)) return;
  double scale = 1;
  for (int i = 0; i < decimals; ++i) scale *= 10;
  double rounded = round(static_cast<double>(v) * scale) / scale;
  if (rounded == 0) rounded = 0;  // turns -0.0 into 0.0
  snprintf(out, kNumBuf, "%.*f", decimals, rounded);
  if (strchr(out, '.') == nullptr) return;
  size_t len = strlen(out);
  while (out[len - 1] == '0') out[--len] = '\0';
  if (out[len - 1] == '.') out[--len] = '\0';
}

void setFloat(JsonDocument& doc, const char* key, const OptF& f, int decimals, char (&buf)[kNumBuf]) {
  formatFixed(f.v, decimals, buf);
  if (f.ok && buf[0] != '\0') {
    doc[key] = serialized(static_cast<const char*>(buf));
  } else {
    doc[key] = nullptr;
  }
}

}  // namespace

size_t buildTelemetryJson(const Reading& r, const Meta& m, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = '\0';
  if (m.device_id == nullptr || m.boot_id == nullptr || m.fw == nullptr) return 0;

  char temperature[kNumBuf], humidity[kNumBuf], lux[kNumBuf];
  JsonDocument doc;
  doc["device_id"] = m.device_id;
  doc["boot_id"] = m.boot_id;
  doc["seq"] = m.seq;
  doc["uptime_ms"] = m.uptime_ms;
  setFloat(doc, "temperature_c", r.temperature_c, 2, temperature);
  setFloat(doc, "humidity_pct", r.humidity_pct, 2, humidity);
  setFloat(doc, "lux", r.lux, 1, lux);
  if (r.soil_raw.ok) {
    doc["soil_raw"] = r.soil_raw.v;
  } else {
    doc["soil_raw"] = nullptr;
  }
  if (m.rssi_dbm.ok) doc["rssi_dbm"] = m.rssi_dbm.v;
  doc["fw"] = m.fw;

  if (doc.overflowed()) return 0;
  size_t len = measureJson(doc);
  if (len >= cap) return 0;  // never emit truncated JSON
  return serializeJson(doc, out, cap);
}
