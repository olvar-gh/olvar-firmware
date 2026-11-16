#include "soil_check.h"

SoilVerdict judgeSoilCheck(const SoilWindow& air, const SoilWindow& water) {
  // int64 so extreme inputs can't overflow the differences.
  const int64_t airRange = static_cast<int64_t>(air.max) - air.min;
  const int64_t span = static_cast<int64_t>(air.last) - water.last;

  if (air.min >= kSoilSaturatedMin) return SoilVerdict::Saturated;
  if (air.max < kSoilNoSignalMax) return SoilVerdict::NoSignal;
  if (airRange > kSoilNoisyRange) return SoilVerdict::Noisy;
  if (-span > kSoilSwappedMargin) return SoilVerdict::Swapped;
  if (span < kSoilStuckSpan) return SoilVerdict::Stuck;
  if (water.settleMs > kSoilSettleLimitMs) return SoilVerdict::Slow;
  if (span < kSoilOkSpan) return SoilVerdict::SmallSpan;
  return SoilVerdict::Ok;
}
