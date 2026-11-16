#include "bh1750_sensor.h"

namespace olvar {

namespace {
constexpr uint8_t kOneTimeHighRes = BH1750::ONE_TIME_HIGH_RES_MODE;  // 0x20
constexpr float kCountsPerLux = 1.2f;  // datasheet conversion at the default MTreg (69)
}  // namespace

// A failed command write returns before the wait, so at most one transaction can time out
// on the waiting path; kWriteMarginMs covers the successful < 1 ms command write.
constexpr uint32_t kWriteMarginMs = 5;
static_assert(Bh1750Sensor::kMeasureMaxMs + I2cBus::kTimeoutMs + kWriteMarginMs <
                  Bh1750Sensor::kReadBudgetMs,
              "BH1750 read() must fit the 200 ms budget even when the final read times out");

Bh1750Sensor::Bh1750Sensor(I2cBus& bus, uint8_t addr) : bus_(bus), bh_(addr), addr_(addr) {}

bool Bh1750Sensor::begin() {
  return bh_.begin(BH1750::ONE_TIME_HIGH_RES_MODE, addr_, &bus_.wire());
}

OptF Bh1750Sensor::read() {
  OptF r;
  TwoWire& wire = bus_.wire();
  // Talk to the chip directly instead of the library's configure()/readLightLevel():
  // those add fixed 10 ms delays, which together with the 180 ms conversion would
  // exceed the budget. One-time mode powers down after each measurement, and a
  // replugged chip powers up with the default MTreg, so this command is all a read needs.
  wire.beginTransmission(addr_);
  wire.write(kOneTimeHighRes);
  if (wire.endTransmission() != 0) {
    bus_.reportResult(false);
    return r;
  }
  uint32_t start = millis();
  while (millis() - start < kMeasureMaxMs) delay(1);
  bool ok = wire.requestFrom(addr_, static_cast<uint8_t>(2)) == 2;
  bus_.reportResult(ok);
  if (!ok) return r;
  uint16_t counts = static_cast<uint16_t>(wire.read()) << 8;
  counts |= static_cast<uint16_t>(wire.read());
  r = {true, counts / kCountsPerLux};
  return r;
}

}  // namespace olvar
