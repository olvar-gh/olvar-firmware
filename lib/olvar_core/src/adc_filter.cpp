#include "adc_filter.h"

#include <algorithm>

namespace {

// Integer division of sum by count, rounded half away from zero.
int32_t roundedMean(int64_t sum, int64_t count) {
  const int64_t half = count / 2;
  const int64_t q = sum >= 0 ? (sum + half) / count : -((-sum + half) / count);
  return static_cast<int32_t>(q);
}

}  // namespace

int32_t trimmedMean(int32_t* samples, size_t n, size_t trimEachSide) {
  if (samples == nullptr || n == 0) return 0;
  std::sort(samples, samples + n);

  if (n == 1 || trimEachSide >= n / 2) {
    const size_t mid = n / 2;
    if (n % 2 == 1) return samples[mid];
    return roundedMean(static_cast<int64_t>(samples[mid - 1]) + samples[mid], 2);
  }

  int64_t sum = 0;
  for (size_t i = trimEachSide; i < n - trimEachSide; ++i) sum += samples[i];
  return roundedMean(sum, static_cast<int64_t>(n - 2 * trimEachSide));
}
