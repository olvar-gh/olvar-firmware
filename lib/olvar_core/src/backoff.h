// Reconnect backoff and a millis()-wraparound-safe interval timer.
// Pure C++17, no Arduino.h, so it builds in native tests.
#pragma once

#include <stdint.h>

// Exponential backoff: the base delay starts at minMs and doubles after every next(), capped
// at maxMs. Each returned delay is the base scaled by ±20 % jitter, so it can be up to
// 1.2 * maxMs.
class Backoff {
 public:
  Backoff(uint32_t minMs, uint32_t maxMs);
  // rand01k is a random number in [0, 1000) (values above 1000 are clamped): 0 gives
  // base * 0.8, 500 gives base, 1000 gives base * 1.2.
  uint32_t next(uint32_t rand01k);
  // Back to minMs, e.g. after a successful connect.
  void reset();

 private:
  uint32_t min_;
  uint32_t max_;
  uint32_t base_;
};

// Fires once every periodMs. Uses unsigned subtraction, so it keeps working when millis()
// wraps from 0xFFFFFFFF to 0.
class IntervalTimer {
 public:
  explicit IntervalTimer(uint32_t periodMs);
  // The first call returns true and starts the period; after that, true once periodMs has
  // passed since the last time it returned true. Missed periods are not caught up.
  bool due(uint32_t nowMs);
  // Starts a new period at nowMs without firing.
  void restart(uint32_t nowMs);

 private:
  uint32_t period_;
  uint32_t last_ = 0;
  bool started_ = false;
};
