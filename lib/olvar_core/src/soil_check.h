// Verdict for the serial `soilcheck` command (PLAN §7.3).
// Diagnostic only: nothing here is stored or sent (decision D).
// Pure C++17, no Arduino.h, so it builds in native tests.
#pragma once

#include <stdint.h>

enum class SoilVerdict { Ok, Saturated, NoSignal, Noisy, Swapped, Stuck, Slow, SmallSpan };

// Stats over one phase (air or water) of the check, in 12-bit ADC counts.
// last is the phase's final value ("the air value" / "the water value").
// settleMs: time from the start of the phase until the reading stayed within
// +/-kSoilSettleBandCounts of last; kSoilNeverSettled if it was still drifting
// when the phase ended (30 s).
struct SoilWindow {
  int32_t min, max, last;
  uint32_t settleMs;
};

// §7.3 thresholds. Rules are checked in this order; the first match wins.
constexpr int32_t kSoilSaturatedMin = 4000;    // Saturated: air.min >= this
constexpr int32_t kSoilNoSignalMax = 300;      // NoSignal:  air.max < this
constexpr int32_t kSoilNoisyRange = 300;       // Noisy:     air.max - air.min > this
constexpr int32_t kSoilSwappedMargin = 150;    // Swapped:   water.last > air.last + this
constexpr int32_t kSoilStuckSpan = 150;        // Stuck:     span < this
constexpr uint32_t kSoilSettleLimitMs = 15000; // Slow:      water.settleMs > this
constexpr int32_t kSoilSettleBandCounts = 30;  // band used by the caller to compute settleMs
constexpr int32_t kSoilOkSpan = 600;           // SmallSpan: span < this; otherwise Ok
constexpr uint32_t kSoilNeverSettled = UINT32_MAX;

// span = air.last - water.last (higher = drier).
SoilVerdict judgeSoilCheck(const SoilWindow& air, const SoilWindow& water);
