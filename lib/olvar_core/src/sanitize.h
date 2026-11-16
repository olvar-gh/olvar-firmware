// Float helpers for telemetry: NaN/Inf never reach the JSON (PLAN §4.1).
// Pure C++17, no Arduino.h, so it builds in native tests.
#pragma once

#include "reading.h"

// ok=false (serialised as null) for NaN and +/-Inf, otherwise {true, v}.
OptF finiteOrNull(float v);

// Rounds v to `decimals` places (0..6), half away from zero.
// Non-finite input is returned unchanged; decimals outside 0..6 are clamped.
float roundTo(float v, int decimals);
