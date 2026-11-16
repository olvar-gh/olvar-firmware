// Status LED patterns (PLAN §5.1): slow blink = no Wi-Fi, fast blink = no MQTT,
// short flash = telemetry published. LedPattern is pure so native tests can cover it.
#pragma once

#include <stdint.h>

namespace olvar {

enum class LedMode { NoWifi, NoMqtt, Online };

class LedPattern {
 public:
  static constexpr uint32_t kSlowHalfPeriodMs = 1000;
  static constexpr uint32_t kFastHalfPeriodMs = 150;
  static constexpr uint32_t kFlashMs = 60;

  void setMode(LedMode mode, uint32_t nowMs) {
    if (mode == mode_) return;
    mode_ = mode;
    since_ = nowMs;
  }
  LedMode mode() const { return mode_; }

  void flash(uint32_t nowMs) {
    flashing_ = true;
    flashAt_ = nowMs;
  }

  bool isOn(uint32_t nowMs) {
    if (flashing_) {
      if (nowMs - flashAt_ < kFlashMs) return true;
      flashing_ = false;
    }
    switch (mode_) {
      case LedMode::NoWifi: return blink(nowMs, kSlowHalfPeriodMs);
      case LedMode::NoMqtt: return blink(nowMs, kFastHalfPeriodMs);
      case LedMode::Online: return false;
    }
    return false;
  }

 private:
  bool blink(uint32_t nowMs, uint32_t halfMs) const { return ((nowMs - since_) / halfMs) % 2 == 0; }

  LedMode mode_ = LedMode::NoWifi;
  uint32_t since_ = 0;
  bool flashing_ = false;
  uint32_t flashAt_ = 0;
};

#ifdef ARDUINO

class StatusLed {
 public:
  explicit StatusLed(uint8_t pin, bool activeHigh = true);

  void begin();
  void setMode(LedMode mode);
  void flashPublished();
  // Call every loop().
  void update();

 private:
  void write(bool on);

  uint8_t pin_;
  bool activeHigh_;
  bool on_ = false;
  LedPattern pattern_;
};

#endif  // ARDUINO

}  // namespace olvar
