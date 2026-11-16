#include "serial_cli.h"

#include <stdlib.h>

#include "cli.h"

namespace olvar {

bool LineReader::push(char c) {
  bool afterCr = lastWasCr_;
  lastWasCr_ = (c == '\r');
  if (c == '\n' && afterCr) return false;
  if (ready_) {
    len_ = 0;
    buf_[0] = '\0';
    overflow_ = false;
    ready_ = false;
  }
  if (c == '\r' || c == '\n') {
    ready_ = true;
    return true;
  }
  if (len_ < kMaxLen) {
    buf_[len_++] = c;
    buf_[len_] = '\0';
  } else {
    overflow_ = true;
  }
  return false;
}

void SoilCheck::enter(uint32_t nowMs) {
  if (phase_ == Phase::WaitAir) beginPhase(Phase::Air, nowMs);
  else if (phase_ == Phase::WaitWater) beginPhase(Phase::Water, nowMs);
}

bool SoilCheck::sampleDue(uint32_t nowMs) const {
  return streaming() && nowMs - phaseStart_ >= nextDueMs_;
}

uint32_t SoilCheck::addSample(uint32_t nowMs, OptI raw) {
  if (!streaming()) return 0;
  uint32_t elapsed = nowMs - phaseStart_;
  // Slots missed while loop() was busy are skipped rather than caught up, so every sample's
  // timestamp is real and settleMs stays honest.
  nextDueMs_ = (elapsed / kSampleEveryMs + 1) * kSampleEveryMs;
  if (raw.ok && count_ < kMaxSamples) {
    values_[count_] = raw.v;
    at_[count_] = elapsed;
    ++count_;
  }
  uint32_t duration = phase_ == Phase::Air ? kAirMs : kWaterMs;
  if (elapsed < duration) return elapsed;
  if (count_ == 0) {
    phase_ = Phase::Failed;
  } else if (phase_ == Phase::Air) {
    air_ = finishPhase();
    phase_ = Phase::WaitWater;
  } else {
    water_ = finishPhase();
    phase_ = Phase::Done;
  }
  return elapsed;
}

void SoilCheck::beginPhase(Phase phase, uint32_t nowMs) {
  phase_ = phase;
  phaseStart_ = nowMs;
  nextDueMs_ = 0;
  count_ = 0;
}

SoilWindow SoilCheck::finishPhase() const {
  SoilWindow w = {values_[0], values_[0], values_[count_ - 1], 0};
  for (size_t i = 1; i < count_; ++i) {
    if (values_[i] < w.min) w.min = values_[i];
    if (values_[i] > w.max) w.max = values_[i];
  }
  size_t i = count_ - 1;
  while (i > 0 && abs(values_[i - 1] - w.last) <= kSoilSettleBandCounts) --i;
  w.settleMs = (count_ > 1 && i == count_ - 1) ? kSoilNeverSettled : at_[i];
  return w;
}

const char* soilVerdictName(SoilVerdict v) {
  switch (v) {
    case SoilVerdict::Ok: return "OK";
    case SoilVerdict::Saturated: return "SATURATED";
    case SoilVerdict::NoSignal: return "NO SIGNAL";
    case SoilVerdict::Noisy: return "NOISY";
    case SoilVerdict::Swapped: return "SWAPPED";
    case SoilVerdict::Stuck: return "STUCK";
    case SoilVerdict::Slow: return "SLOW";
    case SoilVerdict::SmallSpan: return "SMALL SPAN";
  }
  return "?";
}

const char* soilVerdictAdvice(SoilVerdict v) {
  switch (v) {
    case SoilVerdict::Ok:
      return "Good. Dry the sensor, then insert it into the pot up to the white line, always at the "
             "same depth and spot.";
    case SoilVerdict::Saturated:
      return "The pin sees about 3.1 V or more. Unplug the red wire now. Make sure red goes to 3V3 "
             "(not 5V/VIN) and that yellow, not red, goes to GPIO34. Then re-run.";
    case SoilVerdict::NoSignal:
      return "Nothing reaches GPIO34, or the sensor has no power. Re-seat all three wires, check "
             "that yellow is on GPIO34 (not 35/32) and that red/black aren't swapped. Re-run.";
    case SoilVerdict::Noisy:
      return "The value jumps around: a loose wire or a floating pin. Push the yellow wire in "
             "firmly, check the 100 nF capacitor sits between GPIO34 and GND, use shorter wires, "
             "re-run.";
    case SoilVerdict::Swapped:
      return "The water value is higher than the air value: the steps were probably done in the "
             "wrong order. Re-run: air first, then water.";
    case SoilVerdict::Stuck:
      return "The sensor doesn't react to water (oscillator not running or a dead board). Replace "
             "the sensor; pick a listing showing \"TLC555\" on the chip, or DFRobot SEN0193.";
    case SoilVerdict::Slow:
      return "The reading follows water very slowly (floating 1 MOhm resistor defect). Plug a 1 MOhm "
             "resistor (brown-black-green) between the yellow AOUT row and a GND row, then re-run. "
             "If it is still slow, replace the sensor.";
    case SoilVerdict::SmallSpan:
      return "It reacts, but weakly. Make sure the blade is in water up to the line and was dry for "
             "the air step, then re-run. If the span stays small it still works with coarse "
             "resolution; replace when convenient.";
  }
  return "";
}

}  // namespace olvar

#ifdef ARDUINO

#include <WiFi.h>

namespace olvar {

namespace {

bool isBlank(const char* s) {
  for (; *s; ++s) {
    if (*s != ' ' && *s != '\t') return false;
  }
  return true;
}

bool isAbort(const char* s) {
  while (*s == ' ' || *s == '\t') ++s;
  if (*s != 'q' && *s != 'Q') return false;
  return isBlank(s + 1);
}

void printOptF(Stream& io, const char* key, OptF v, int decimals) {
  if (v.ok) io.printf(" %s=%.*f", key, decimals, v.v);
  else io.printf(" %s=null", key);
}

}  // namespace

SerialCli::SerialCli(Stream& io, Sht31Sensor& sht, Bh1750Sensor& bh, SoilSensor& soil,
                     CliHooks hooks)
    : io_(io), sht_(sht), bh_(bh), soil_(soil), hooks_(hooks) {}

void SerialCli::begin() { io_.println("Type 'help' for commands."); }

void SerialCli::poll() {
  if (reader_.pump(io_, kMaxBytesPerPoll)) {
    if (reader_.overflowed()) {
      io_.printf("Line too long (max %u chars), ignored.\n", static_cast<unsigned>(LineReader::kMaxLen));
    } else if (check_.active()) {
      handleSoilCheckLine(reader_.line());
    } else {
      handleLine(reader_.line());
    }
  }
  if (check_.sampleDue(millis())) stepSoilCheck();
}

void SerialCli::handleLine(const char* line) {
  switch (parseCommand(line)) {
    case Cmd::Status: printStatus(); break;
    case Cmd::Raw: printRaw(); break;
    case Cmd::SoilCheck: startSoilCheck(); break;
    case Cmd::Portal:
      if (!hooks_.requestPortal) {
        io_.println("Portal is not available in this build.");
        break;
      }
      io_.println("Opening the setup portal: rebooting...");
      hooks_.requestPortal();
      reboot();
      break;
    case Cmd::Reboot:
      io_.println("Rebooting...");
      reboot();
      break;
    case Cmd::Help: printHelp(); break;
    case Cmd::Unknown:
      if (!isBlank(line)) io_.printf("Unknown command '%s'. Type 'help'.\n", line);
      break;
  }
}

void SerialCli::printHelp() {
  io_.println("Commands:");
  io_.println("  status     network, MQTT, ids, seq, skipped, free heap");
  io_.println("  raw        one fresh soil_raw + SHT31 + BH1750 reading (wiring check)");
  io_.println("  soilcheck  guided air/water soil sensor check (~2 min, nothing is saved)");
  io_.println("  portal     reopen the Wi-Fi/MQTT setup portal (reboots)");
  io_.println("  reboot     restart the board");
  io_.println("  help       this list");
}

void SerialCli::printStatus() {
  CliStatus s;
  if (hooks_.status) hooks_.status(s);
  bool wifi = WiFi.status() == WL_CONNECTED;
  io_.printf("fw        %s\n", OLVAR_FW_VERSION);
  io_.printf("device_id %s\n", s.device_id);
  io_.printf("boot_id   %s\n", s.boot_id);
  if (wifi) {
    io_.printf("wifi      connected, ip %s, rssi %d dBm\n", WiFi.localIP().toString().c_str(),
               static_cast<int>(WiFi.RSSI()));
  } else {
    io_.println("wifi      not connected");
  }
  io_.printf("mqtt      %s\n", s.mqtt_state);
  io_.printf("seq       %lu\n", static_cast<unsigned long>(s.seq));
  io_.printf("skipped   %lu\n", static_cast<unsigned long>(s.skipped));
  io_.printf("heap      %lu free, %lu min\n", static_cast<unsigned long>(ESP.getFreeHeap()),
             static_cast<unsigned long>(ESP.getMinFreeHeap()));
  io_.printf("uptime    %lu ms\n", static_cast<unsigned long>(millis()));
}

void SerialCli::printRaw() {
  OptI soil = soil_.sampleRaw();
  Sht31Result sht = sht_.read();
  OptF lux = bh_.read();
  if (soil.ok) io_.printf("soil_raw=%ld", static_cast<long>(soil.v));
  else io_.print("soil_raw=null");
  printOptF(io_, "temperature_c", sht.temperature_c, 2);
  printOptF(io_, "humidity_pct", sht.humidity_pct, 2);
  printOptF(io_, "lux", lux, 1);
  io_.println();
}

void SerialCli::startSoilCheck() {
  check_.start();
  io_.println("Soil sensor check (nothing is saved or sent). Type 'q' + Enter to abort.");
  io_.println("Step 1/2: hold the sensor in AIR by its top edge, blade dry and untouched, press Enter.");
}

void SerialCli::handleSoilCheckLine(const char* line) {
  if (isAbort(line)) {
    check_.abort();
    io_.println("soilcheck aborted.");
    return;
  }
  if (check_.waitingForEnter()) {
    bool water = check_.phase() == SoilCheck::Phase::WaitWater;
    check_.enter(millis());
    io_.printf("Measuring %s for %lu s...\n", water ? "WATER" : "AIR",
               static_cast<unsigned long>((water ? SoilCheck::kWaterMs : SoilCheck::kAirMs) / 1000));
    return;
  }
  io_.println("soilcheck is running; type 'q' + Enter to abort.");
}

void SerialCli::stepSoilCheck() {
  bool water = check_.phase() == SoilCheck::Phase::Water;
  OptI raw = soil_.sampleRaw();
  uint32_t elapsed = check_.addSample(millis(), raw);
  const char* label = water ? "water" : "air  ";
  if (raw.ok) io_.printf("  %s %2lu s  soil_raw=%ld\n", label, static_cast<unsigned long>(elapsed / 1000), static_cast<long>(raw.v));
  else io_.printf("  %s %2lu s  soil_raw=null (read failed)\n", label, static_cast<unsigned long>(elapsed / 1000));

  switch (check_.phase()) {
    case SoilCheck::Phase::WaitWater:
      io_.printf("Air: min %ld, max %ld, value %ld.\n", static_cast<long>(check_.air().min),
                 static_cast<long>(check_.air().max), static_cast<long>(check_.air().last));
      io_.println("Step 2/2: stand the blade in a glass of WATER up to the white line (keep the "
                  "electronics dry), press Enter.");
      break;
    case SoilCheck::Phase::Done: printSoilResult(); break;
    case SoilCheck::Phase::Failed:
      io_.println("soilcheck failed: no valid soil reading in this phase. Check the wiring and re-run.");
      break;
    default: break;
  }
}

void SerialCli::printSoilResult() {
  const SoilWindow& a = check_.air();
  const SoilWindow& w = check_.water();
  io_.printf("Air value   %ld (min %ld, max %ld)\n", static_cast<long>(a.last), static_cast<long>(a.min), static_cast<long>(a.max));
  io_.printf("Water value %ld (min %ld, max %ld)\n", static_cast<long>(w.last), static_cast<long>(w.min), static_cast<long>(w.max));
  io_.printf("Span        %ld (air - water; expect >= %ld)\n", static_cast<long>(check_.span()), static_cast<long>(kSoilOkSpan));
  if (w.settleMs == kSoilNeverSettled) io_.println("Water settle still drifting at the end");
  else io_.printf("Water settle %.1f s (limit %.0f s)\n", w.settleMs / 1000.0, kSoilSettleLimitMs / 1000.0);
  SoilVerdict v = check_.verdict();
  io_.printf("Verdict: %s\n%s\n", soilVerdictName(v), soilVerdictAdvice(v));
}

void SerialCli::reboot() {
  io_.flush();
  if (hooks_.reboot) hooks_.reboot();
  else ESP.restart();
}

}  // namespace olvar

#endif  // ARDUINO
