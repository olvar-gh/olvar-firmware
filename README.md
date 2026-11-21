# olvar-firmware

ESP32 firmware for Olvar, a plant monitor. It reads temperature, humidity, light and soil
moisture once a minute and publishes them over MQTT to the Olvar gateway on a Raspberry Pi.
The design and the telemetry contract are in [docs/PLAN.md](docs/PLAN.md); what changed per
release is in [CHANGELOG.md](CHANGELOG.md).

## Where it fits

Olvar is three repositories:

- **olvar-firmware** (this one): the ESP32 sensor node.
- **olvar-gateway**: Mosquitto and a bridge on a Raspberry Pi 3B that store the telemetry in a
  hosted Supabase project, plus the database schema and the Claude-backed plant chat.
- **olvar-dashboard**: the Expo app, deployed to Cloudflare Pages on the web and built locally
  for iOS.

The firmware only talks MQTT on the local network; it never reaches Supabase or the internet.

## Hardware

- 38-pin **ESP32-WROOM-32D** dev board (PlatformIO board `esp32dev`) on an expansion board,
  powered over USB.
- Temperature and humidity: an **SHT31** on I2C (0x44 or 0x45), or as a fallback a **DHT11** on
  **GPIO4**. The firmware uses whichever answers at boot and reports it in telemetry as
  `climate_sensor`.
- Light: a **GY-302 (BH1750)** on I2C 0x23. I2C is GPIO21 (SDA) / GPIO22 (SCL).
- Soil: a generic **capacitive soil moisture probe** on **GPIO34** (ADC1), powered from 3V3.

Wiring, including the DHT11 variant: [docs/WIRING.md](docs/WIRING.md).

## Quick start

1. Install PlatformIO Core 6.x (`brew install platformio`, `pipx install platformio`, or the
   VS Code "PlatformIO IDE" extension). Native tests also need a host compiler
   (`xcode-select --install` on macOS, `build-essential` on Debian/Ubuntu).
2. Run the host tests and build the firmware:
   ```sh
   pio test -e native
   pio run -e esp32dev
   ```
3. Flash over USB and open the serial monitor (115200 baud):
   ```sh
   pio run -e esp32dev -t upload && pio device monitor
   ```
4. Join the board's `olvar-setup-xxxxxx` Wi-Fi from a phone and enter your Wi-Fi network and the
   broker's address: [docs/PROVISIONING.md](docs/PROVISIONING.md).

## Local dev defaults (optional)

The build needs no secrets. Wi-Fi and MQTT settings are entered in the provisioning portal
and stored in NVS. For dev fallbacks, copy `secrets.example.ini` to `secrets.ini` and edit it.
`secrets.ini` is gitignored; never commit it. `secrets.defaults.ini` stays empty.

## Telemetry

One JSON message every 60 s on `olvar/v1/<device_id>/telemetry` (QoS 1), plus a retained
`online`/`offline` on `olvar/v1/<device_id>/status`. The exact payload is in
[docs/contract/telemetry.schema.json](docs/contract/telemetry.schema.json) with an example in
[docs/contract/telemetry.example.json](docs/contract/telemetry.example.json).

To develop the gateway without hardware, `tools/sim_telemetry.sh` publishes the same payload as a
simulated device (needs `mosquitto_pub` and `openssl`; usage is at the top of the script).

## Layout

- `lib/olvar_core/`: hardware-independent logic (pure C++17, no `Arduino.h`), tested natively.
- `src/`: Arduino adapters (sensors, Wi-Fi, MQTT, NVS, serial console, LED) and the entry point.
- `include/pins.h`: GPIO numbers, I2C addresses, telemetry interval.
- `test/native/`: host tests (`pio test -e native`), run in CI.
- `test/embedded/`: on-board tests (`pio test -e esp32dev`, needs the wired board). These link
  `src/` (`test_build_src = yes`); `src/main.cpp` is excluded via `#ifndef PIO_UNIT_TESTING`.
- `tools/sim_telemetry.sh`: MQTT telemetry simulator.

The firmware version is `custom_fw_version` in `platformio.ini`; `scripts/version.py`
appends the git short hash (e.g. `0.1.0+a1b2c3d`) as `OLVAR_FW_VERSION`. The board prints it at
boot, in `status`, and sends it as `fw` in every message.

## Bring-up

First time with real hardware: follow [docs/BRINGUP.md](docs/BRINGUP.md), from the wiring check to
seeing telemetry arrive on the Pi.

## License

[MIT](LICENSE).
