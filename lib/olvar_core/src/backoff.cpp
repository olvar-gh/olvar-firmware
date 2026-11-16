#include "backoff.h"

Backoff::Backoff(uint32_t minMs, uint32_t maxMs)
    : min_(minMs), max_(maxMs < minMs ? minMs : maxMs), base_(minMs) {}

uint32_t Backoff::next(uint32_t rand01k) {
  if (rand01k > 1000) rand01k = 1000;
  // Factor in thousandths: 800..1200. 64-bit so base * factor can't overflow.
  const uint64_t factor = 800 + static_cast<uint64_t>(rand01k) * 400 / 1000;
  const uint32_t delay = static_cast<uint32_t>(static_cast<uint64_t>(base_) * factor / 1000);
  base_ = (base_ > max_ / 2) ? max_ : base_ * 2;
  return delay;
}

void Backoff::reset() { base_ = min_; }

IntervalTimer::IntervalTimer(uint32_t periodMs) : period_(periodMs) {}

bool IntervalTimer::due(uint32_t nowMs) {
  if (started_ && static_cast<uint32_t>(nowMs - last_) < period_) return false;
  restart(nowMs);
  return true;
}

void IntervalTimer::restart(uint32_t nowMs) {
  last_ = nowMs;
  started_ = true;
}
