# olvar-firmware

ESP32 firmware that reads the plant sensors (SHT31, BH1750, capacitive soil) and publishes
telemetry over MQTT to the Olvar gateway. The full plan and contract are in [docs/PLAN.md](docs/PLAN.md).

## Quick start

1. Install PlatformIO Core 6.x (`brew install platformio`, or the VS Code "PlatformIO IDE"
   extension). Native tests also need a host compiler (`xcode-select --install` on macOS).
2. Run the host tests and build the firmware:
   ```sh
   pio test -e native
   pio run -e esp32dev
   ```
3. Flash over USB and open the serial monitor (115200 baud):
   ```sh
   pio run -e esp32dev -t upload && pio device monitor
   ```

## Local dev defaults (optional)

The build needs no secrets. Wi-Fi and MQTT settings are entered in the provisioning portal
and stored in NVS. For dev fallbacks, copy `secrets.example.ini` to `secrets.ini` and edit it.
`secrets.ini` is gitignored; never commit it. `secrets.defaults.ini` stays empty.

## Layout

- `lib/olvar_core/`: hardware-independent logic (pure C++17, no `Arduino.h`), tested natively.
- `src/`: Arduino adapters and the entry point.
- `include/pins.h`: GPIO numbers, I2C addresses, telemetry interval.
- `test/native/`: host tests (`pio test -e native`).
- `test/embedded/`: on-board tests (`pio test -e esp32dev`). These link `src/`
  (`test_build_src = yes`); `src/main.cpp` is excluded via `#ifndef PIO_UNIT_TESTING`.

The firmware version is `custom_fw_version` in `platformio.ini`; `scripts/version.py`
appends the git short hash (e.g. `0.1.0+a1b2c3d`) as `OLVAR_FW_VERSION`.
