// Serial CLI (PLAN T7): status, raw, soilcheck, dhtwatch, portal, reboot, help.
// LineReader, SoilCheck and DhtWatch are pure so native tests can cover them; SerialCli is the
// Arduino side and is non-blocking apart from the sensor reads a command asks for.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "reading.h"
#include "soil_check.h"

namespace olvar {

class LineReader {
 public:
  static constexpr size_t kMaxLen = 63;

  // True when c completes a line; read it with line() before the next push().
  // "\r", "\n" and "\r\n" each end exactly one line, so Enter is never seen twice.
  bool push(char c);
  const char* line() const { return buf_; }
  bool overflowed() const { return overflow_; }

  // Reads up to maxBytes from src (anything with available() and read()) and stops at the
  // first completed line, so one poll() never runs more than one command: commands like
  // `raw` block for ~260 ms each and a burst of them must not stall loop().
  template <typename Src>
  bool pump(Src& src, int maxBytes) {
    for (; maxBytes > 0 && src.available() > 0; --maxBytes) {
      int c = src.read();
      if (c < 0) return false;
      if (push(static_cast<char>(c))) return true;
    }
    return false;
  }

 private:
  char buf_[kMaxLen + 1] = {};
  size_t len_ = 0;
  bool overflow_ = false;
  bool ready_ = false;
  bool lastWasCr_ = false;
};

// Guided air/water check of PLAN §7.3. Diagnostic only: nothing is stored or published.
class SoilCheck {
 public:
  enum class Phase { Idle, WaitAir, Air, WaitWater, Water, Done, Failed };

  static constexpr uint32_t kSampleEveryMs = 1000;
  static constexpr uint32_t kAirMs = 20000;
  static constexpr uint32_t kWaterMs = 30000;
  static constexpr size_t kMaxSamples = kWaterMs / kSampleEveryMs + 1;

  void start() { phase_ = Phase::WaitAir; }
  void abort() { phase_ = Phase::Idle; }
  Phase phase() const { return phase_; }
  bool active() const { return phase_ != Phase::Idle && phase_ != Phase::Done && phase_ != Phase::Failed; }
  bool waitingForEnter() const { return phase_ == Phase::WaitAir || phase_ == Phase::WaitWater; }
  bool streaming() const { return phase_ == Phase::Air || phase_ == Phase::Water; }

  void enter(uint32_t nowMs);
  bool sampleDue(uint32_t nowMs) const;
  // Elapsed ms of the sample within its phase. A failed read (ok=false) is skipped; a phase
  // that ends without one good sample moves to Failed.
  uint32_t addSample(uint32_t nowMs, OptI raw);

  const SoilWindow& air() const { return air_; }
  const SoilWindow& water() const { return water_; }
  int32_t span() const { return air_.last - water_.last; }
  SoilVerdict verdict() const { return judgeSoilCheck(air_, water_); }

 private:
  void beginPhase(Phase phase, uint32_t nowMs);
  SoilWindow finishPhase() const;

  Phase phase_ = Phase::Idle;
  uint32_t phaseStart_ = 0;
  uint32_t nextDueMs_ = 0;
  int32_t values_[kMaxSamples] = {};
  uint32_t at_[kMaxSamples] = {};
  size_t count_ = 0;
  SoilWindow air_ = {};
  SoilWindow water_ = {};
};

// `dhtwatch`: one DHT11 read every 2 s for 30 s, for a breath test. Diagnostic only.
class DhtWatch {
 public:
  static constexpr uint32_t kDurationMs = 30000;

  void start(uint32_t nowMs);
  void abort() { active_ = false; }
  bool active() const { return active_; }
  // sensorDueAtMs: Dht11Sensor::dueAtMs(), so every read is a real transaction (2 s apart).
  bool due(uint32_t nowMs, uint32_t sensorDueAtMs) const {
    return active_ && static_cast<int32_t>(nowMs - sensorDueAtMs) >= 0;
  }
  // Records one read and returns its ms since start(); ends the watch after kDurationMs.
  uint32_t add(uint32_t nowMs, const TempHumidity& r);

  uint8_t reads() const { return reads_; }
  uint8_t failed() const { return failed_; }
  // Lowest and highest humidity_pct seen; ok=false when no read succeeded.
  OptF humidityMin() const { return hMin_; }
  OptF humidityMax() const { return hMax_; }

 private:
  bool active_ = false;
  uint32_t startMs_ = 0;
  uint8_t reads_ = 0;
  uint8_t failed_ = 0;
  OptF hMin_, hMax_;
};

const char* soilVerdictName(SoilVerdict v);
const char* soilVerdictAdvice(SoilVerdict v);

}  // namespace olvar

#ifdef ARDUINO

#include <Arduino.h>

#include <functional>

#include "sensors/bh1750_sensor.h"
#include "sensors/climate_sensor.h"
#include "sensors/soil_sensor.h"

namespace olvar {

struct CliStatus {
  const char* device_id = "";
  const char* boot_id = "";
  const char* mqtt_state = "unknown";
  uint32_t seq = 0;
  uint32_t skipped = 0;
  // Multi-line link reason records (this boot and the previous one), or null.
  const char* link = nullptr;
};

// Wired by the app (T8) to the settings store, provisioning and MQTT link.
struct CliHooks {
  std::function<void(CliStatus&)> status;
  std::function<void()> requestPortal;
  std::function<void()> reboot;
};

class SerialCli {
 public:
  static constexpr uint32_t kBaud = 115200;
  static constexpr int kMaxBytesPerPoll = 128;
  static constexpr size_t kDiagLen = 128;  // formatDht11Diag() output

  SerialCli(Stream& io, ClimateSensor& climate, Bh1750Sensor& bh, SoilSensor& soil, CliHooks hooks);

  // Expects the port to be open already: Serial.begin(SerialCli::kBaud).
  void begin();
  // Call every loop(). Runs at most one command and one soilcheck sample (~65 ms) per call.
  void poll();
  bool soilCheckActive() const { return check_.active(); }
  bool dhtWatchActive() const { return watch_.active(); }

 private:
  void handleLine(const char* line);
  void handleSoilCheckLine(const char* line);
  void printHelp();
  void printStatus();
  void printRaw();
  void printDhtDiag();
  void startDhtWatch();
  void handleDhtWatchLine(const char* line);
  void stepDhtWatch();
  void startSoilCheck();
  void stepSoilCheck();
  void printSoilResult();
  void reboot();

  Stream& io_;
  ClimateSensor& climate_;
  Bh1750Sensor& bh_;
  SoilSensor& soil_;
  CliHooks hooks_;
  LineReader reader_;
  SoilCheck check_;
  DhtWatch watch_;
};

}  // namespace olvar

#endif  // ARDUINO
