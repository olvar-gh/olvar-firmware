#include "sanitize.h"

#include <math.h>

OptF finiteOrNull(float v) {
  OptF out;
  if (isfinite(v)) {
    out.ok = true;
    out.v = v;
  }
  return out;
}

float roundTo(float v, int decimals) {
  if (!isfinite(v)) return v;
  if (decimals < 0) decimals = 0;
  if (decimals > 6) decimals = 6;
  double scale = 1.0;
  for (int i = 0; i < decimals; ++i) scale *= 10.0;
  // Work in double so the float's exact value (e.g. 2.5f, 0.125f) scales without error;
  // round() rounds half away from zero.
  return static_cast<float>(round(static_cast<double>(v) * scale) / scale);
}
