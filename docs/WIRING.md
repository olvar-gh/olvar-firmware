# Wiring the Olvar sensors

This guide is for first-timers. Read each step fully before you do it, and **keep the USB
cable unplugged while you move wires**. Source: docs/PLAN.md §7.3–§7.4.

## What you need

| Part | Notes |
|---|---|
| 38-pin ESP32-WROOM-32D dev board on its expansion board | the expansion board has a **3.3 V / 5 V jumper** |
| Breadboard + male-to-male / male-to-female jumper wires | keep I2C wires ≤ 30 cm |
| SHT31 breakout (temperature + humidity) | pins VIN, GND, SCL, SDA, (ADR). **Or** a DHT11 instead, see [below](#dht11-instead-of-the-sht31) |
| GY-302 breakout (BH1750 light sensor) | pins VCC, GND, SCL, SDA, ADDR |
| Capacitive soil moisture sensor + its 3-wire cable | pins VCC, GND, AOUT (printed on the board; the common cable colours are red, black, yellow) |
| One 100 nF ceramic capacitor (recommended) | small, often marked **"104"**; it has no polarity. Optional, but it steadies the soil reading |
| A data USB cable and a 5 V ≥ 1 A phone charger | a charge-only cable won't show a serial port |

## The three rules

1. **Everything runs on 3.3 V.** Never connect a sensor to **5V** or **VIN** on the ESP32.
2. **Set the expansion board's V-rail jumper to 3.3 V** before plugging anything in. That jumper
   decides the voltage on every "V" pin of the G-V-S headers.
3. **Unplug USB before changing any wire.**

## The picture

```
 ESP32 pin        breadboard row      sensor pins plugged into that row
 ─────────        ──────────────      ─────────────────────────────────
 3V3     ───────► 3V3 row       ◄──── SHT31 VIN, GY-302 VCC, soil VCC
 GND     ───────► GND row       ◄──── SHT31 GND, GY-302 GND, soil GND,
                                      SHT31 ADR, GY-302 ADDR, capacitor leg 2
 GPIO21  ───────► SDA row       ◄──── SHT31 SDA, GY-302 SDA
 GPIO22  ───────► SCL row       ◄──── SHT31 SCL, GY-302 SCL
 GPIO34  ───────► SOIL row      ◄──── soil AOUT, capacitor leg 1
```

Both I2C sensors share the same two signal wires (the "I2C bus"). Make **one breadboard row for
SDA** and **one row for SCL**, run one wire from each row to the ESP32, and plug both sensors'
SDA into the SDA row and both SCL into the SCL row. It's also handy to make a 3V3 row and a GND row.

## Wiring table

**First:** set the expansion board's **V-rail jumper to 3.3 V**.

| Wire | From (sensor pin) | To (ESP32 / expansion board) | Notes |
|---|---|---|---|
| 1 | SHT31 **VIN** (or VCC) | **3V3** | never 5 V |
| 2 | SHT31 **GND** | **GND** | |
| 3 | SHT31 **SDA** | **GPIO21** (SDA row) | shared bus |
| 4 | SHT31 **SCL** | **GPIO22** (SCL row) | shared bus |
| 5 | SHT31 **ADR/ADDR** (if present) | **GND**, or leave unconnected if the board has a pull-down | → address **0x44** |
| 6 | GY-302 **VCC** | **3V3** | |
| 7 | GY-302 **GND** | **GND** | |
| 8 | GY-302 **SDA** | SDA row (→ GPIO21) | |
| 9 | GY-302 **SCL** | SCL row (→ GPIO22) | |
| 10 | GY-302 **ADDR** | **GND** or leave unconnected | → address **0x23**; never 5 V |
| 11 | Soil sensor **VCC** (often red) | **3V3** | |
| 12 | Soil sensor **GND** (often black) | **GND** | |
| 13 | Soil sensor **AOUT** (often yellow) | **GPIO34** | ADC1_CH6, input-only; **not** an ADC2 pin |
| 14 | 100 nF ceramic capacitor ("104"), **recommended** | between **GPIO34** and **GND**, close to the ESP32 | less ADC noise; the firmware also averages 64 samples, so it works without one |
| 15 | USB cable | ESP32 board's USB port → 5 V ≥1 A charger (or your Mac while flashing) | |

**SHT31 boards with a 2×3 header:** some SHT31 breakouts have their six pins in two rows of
three. **Don't push such a header into one side of a breadboard**: every five-hole breadboard row
is one connection, so the two pins that land in the same row are shorted together (for example
VIN to GND). Either plug it across the centre gap so each pin gets its own row, or connect it with
female-to-male jumper wires.

**Pins not to use for sensors:** GPIO0/2/5/12/15 (boot strapping), GPIO6–11 and 16–17 (flash),
and all ADC2 pins for analog (ADC2 stops working while Wi-Fi is on).

**Placement:** put the SHT31 on 15–20 cm wires **away from the ESP32 and the charger**, out of
direct sun (their heat makes the temperature read high and the humidity low). Point the GY-302
window up at plant level, uncovered. Keep I2C wires ≤ 30 cm.

### Before you plug in USB: a 30-second double-check

- [ ] Jumper on **3.3 V**.
- [ ] No wire goes to **5V** or **VIN**.
- [ ] Soil **AOUT** (not VCC) goes to **GPIO34**.
- [ ] SDA → GPIO21 and SCL → GPIO22 (swapping them is the most common mistake; nothing breaks,
      the sensors just aren't found).
- [ ] If you fitted the capacitor: its two legs are in the GPIO34 row and a GND row.
- [ ] Only one temperature/humidity sensor: the SHT31 **or** the DHT11 (if both answer, the SHT31
      is used and the DHT11 is ignored).

## DHT11 instead of the SHT31

If you have no working SHT31, a **DHT11** can measure temperature and humidity instead. The
firmware looks for an SHT31 at boot (address 0x44, then 0x45). If neither answers it uses the
DHT11 on **GPIO4**. The telemetry is the same; the DHT11 is just coarser (whole degrees and whole
percent, ±2 °C, ±5 %RH). The ASAIR datasheet (2022-12) gives a reporting range of −20–60 °C and
5–95 %RH, accurate in 8–85 %RH; older Aosong sheets said 0–50 °C and 20–90 %RH. A DHT11 stuck
near 5 %RH in a normal room is at the bottom of its range: see
[BRINGUP.md §10](BRINGUP.md#10-humidity-check-dht11).

**Your part:** the bare 4-pin ASAIR DHT11 (blue grille, no small board around it), 3.3–5.5 V.
Hold it with the **grille facing you and the pins pointing down**. Counted **left to right**:

```
      ┌───────────────────┐
      │ ▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒ │   grille facing you
      │ ▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒ │
      └──┬────┬────┬────┬─┘
         │    │    │    │     pins down
         1    2    3    4
        VDD  DATA  NC  GND
```

| Wire | DHT11 pin | To | Notes |
|---|---|---|---|
| D1 | **1 VDD** | **3V3** | never 5 V: at 5 V, DATA would swing to 5 V on a 3.3 V pin |
| D2 | **2 DATA** | **GPIO4** | |
| D3 | 10 kΩ resistor (brown-black-orange) | between **DATA** (pin 2) and **3V3** | the pull-up; the bare part has none built in |
| D4 | **3 NC** | nothing | not connected inside |
| D5 | **4 GND** | **GND** | |

On the breadboard: put the four legs in four different rows, run wires from the pin 1 row to the
3V3 row, from the pin 4 row to the GND row and from the pin 2 row to GPIO4, and plug the 10 kΩ
resistor's legs into the pin 2 row and the 3V3 row. Leave the SHT31 out. Placement is the same as
for the SHT31: 15–20 cm away from the ESP32 and the charger.

GPIO4 is not a boot-strapping pin, so the pull-up doesn't change how the board starts. The
firmware waits 2 s after power-up and 2 s after each read before talking to the DHT11 again (its
limits). The DHT11 answers with the sample it took after the previous read, so each telemetry
message reads it twice, 2 s apart, and sends the second, current value; that message goes out
about 2 s later than with an SHT31. A read with a bad checksum or no answer is sent as `null`,
never as an old value. `status` and `raw` in the serial monitor show
which sensor is in use (`DHT11 on GPIO4` or `SHT31 at 0x44`).

## Soil sensor: safe wiring and software check

**Your model:** a generic capacitive soil moisture sensor, 3.3–5.5 V. The seller's manual says a
built-in regulator and 0–3.0 V output; the version isn't stated. Its three pins are **VCC**, **GND**
and **AOUT**. The usual cable colours are red = VCC, black = GND, yellow = AOUT, but not every
board uses them (some use blue for the signal). **Check the pin labels printed on your board next
to the connector, or its datasheet, before applying power**, and follow the labels, not the colours.

**Safe default wiring (always use this):**
- **VCC → 3V3**: the board's 3V3 pin, or any expansion-board V pin with the jumper on 3.3 V.
  **Never 5V or VIN.** If your board happens to lack the regulator, 5 V could push AOUT above
  3.3 V and damage GPIO34. At 3.3 V that can't happen, and a board with a regulator works fine on 3.3 V.
- **GND → GND**.
- **AOUT → GPIO34**, plus (recommended) a 100 nF capacitor from GPIO34 to GND (wire 14 above).
- Don't put it in the pot yet. Before the check, wipe the blade dry.

**Software check** (after flashing; takes ~2 minutes):
1. Run `pio device monitor` and type `soilcheck`, then Enter.
2. When asked, **hold the sensor in the air** by its top edge, not touching the blade. Press Enter
   and wait 20 s.
3. When asked, **stand the blade in a glass of water up to the white line**. Keep the electronics
   dry. Press Enter and wait 30 s.
4. The firmware prints the air value, the water value, the span (air − water) and a verdict.

**What to expect** (12-bit counts, 3.3 V supply): air **≈ 2400–3900**, water **≈ 900–2300**,
span **≥ 600** (usually 1000–2000). Higher = drier. The exact numbers differ per sensor; the
dashboard calibration uses your own values.

| Verdict (checked in this order) | Rule | What it means → what to do |
|---|---|---|
| **Saturated** | air min ≥ 4000 | The pin sees ≥ ~3.1 V. **Unplug the VCC wire now.** Make sure it's on **3V3**, not 5V/VIN, and that AOUT (not VCC) goes to GPIO34. Then re-run. |
| **NoSignal** | air max < 300 | Nothing reaches GPIO34, or the sensor has no power. Re-seat all three wires and check that AOUT is on **GPIO34** (not 35/32) and VCC/GND aren't swapped. Re-run. |
| **Noisy** | max − min > 300 within the air phase | The value jumps around: a loose wire or a floating pin (an unconnected ADC pin reads random values). Push the AOUT wire in firmly, add the 100 nF capacitor between GPIO34 and GND if it isn't fitted (or check it sits right), use shorter wires, re-run. |
| **Swapped** | water value > air value + 150 | The steps were probably done in the wrong order. Re-run: air first, then water. |
| **Stuck** | span < 150 | The sensor doesn't react to water. Its oscillator isn't running, which is typical of boards with a 5 V-only timer chip, or it's a dead board. **Replace the sensor**; they're about $1–2. When reordering, choose a listing whose photos show "TLC555" on the chip, or a branded one (DFRobot SEN0193). |
| **Slow** | water value not settled (within ±30) after 15 s, or still drifting at 30 s | The reading follows water very slowly: the known "floating 1 MΩ resistor" defect. Fix without soldering: plug a **1 MΩ resistor** (colour bands brown-black-green) into the breadboard **between the AOUT row and a GND row**. Re-run; it should now settle in a few seconds. If it's still slow, replace the sensor. |
| **SmallSpan** | 150 ≤ span < 600 | It reacts, but weakly. Make sure the blade is in water **up to the line** and was **dry** for the air step, then re-run. If the span stays small it still works, but with coarse resolution; replace when convenient. |
| **OK** | everything else (span ≥ 600) | Good. Dry the sensor, then insert it into the pot up to the line, always at the same depth and spot. |

After a passing check, protect the sensor: coat the cut PCB edges along the blade and the
electronics at the top (nail varnish, PlastiDip or heat-shrink). **Never push it in past the white
line.** Then calibrate in the dashboard (Soil calibration screen; see PLAN.md §7.6).

## Checking the wiring with the on-board tests

With the board plugged into your Mac, run:

```sh
pio test -e esp32dev -f embedded/test_i2c_bus
pio test -e esp32dev -f embedded/test_sensors -v
```

| Test | Passes when | If it fails |
|---|---|---|
| I2C scan | a device at **0x23** (GY-302), and at **0x44** or **0x45** if an SHT31 is fitted | check SDA/SCL aren't swapped, 3V3 + GND reach both boards, ADR/ADDR not on 5 V |
| Temperature/humidity | SHT31: 5–40 °C and 10–95 %RH. DHT11: 0–50 °C and, indoors, 20–60 %RH (below 10 %RH: [BRINGUP.md §10](BRINGUP.md#10-humidity-check-dht11)) | move it away from the ESP32 and charger. "no SHT31 … and no DHT11": recheck its wires; for the DHT11 also the 10 kΩ resistor and DATA on GPIO4 |
| BH1750 | lux > 0 in room light; ≤ 5 when you cover the window (the test tells you when) | window covered by tape or facing the table |
| Soil | air − water > 500 counts (the test tells you when to hold it in air, then in water) | run `soilcheck` above for a diagnosis |

The test runner can't pass keyboard input to the board, so the hand steps are timed. Watch the
output: when a line starts with `>>>`, do what it says before the countdown reaches 0
(10 s to cover the light sensor, 15 s to hold the soil sensor in air, 20 s to put it in water).
Keep the light sensor covered until the air step tells you to uncover it.
