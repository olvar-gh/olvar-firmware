// Multisample filter for the soil ADC (PLAN §3 "Soil sampling").
// Pure C++17, no Arduino.h, so it builds in native tests.
#pragma once

#include <stddef.h>
#include <stdint.h>

// Sorts samples[0..n) in place, drops trimEachSide values from each end and
// returns the mean of the rest, rounded half away from zero.
// If n == 1 or trimEachSide >= n/2 (nothing would be left), returns the median
// instead (for even n: mean of the two middle values, same rounding).
// Returns 0 when n == 0 or samples is null.
int32_t trimmedMean(int32_t* samples, size_t n, size_t trimEachSide);
