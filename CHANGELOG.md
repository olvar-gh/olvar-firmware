# Changelog

All notable changes to this firmware. Versions follow [SemVer](https://semver.org); the build
appends the git short hash (`0.1.0+a1b2c3d`).

## 0.1.0

First public release. Tested on a 38-pin ESP32-WROOM-32D talking to the Olvar gateway on a
Raspberry Pi 3B.

### What it does

- **Sensors.** Temperature and humidity from an SHT31 on I2C (0x44, then 0x45), or a DHT11 on
  GPIO4 when no SHT31 answers at boot; light from a GY-302 (BH1750) at 0x23; soil moisture as a
  raw 12-bit ADC count from a capacitive probe on GPIO34 (trimmed mean of 64 samples). A failed
  read is sent as `null`, never as NaN or an old value.
- **Telemetry.** One JSON message every 60 s on `olvar/v1/<device_id>/telemetry`, QoS 1, with
  `device_id`, a random per-boot `boot_id`, `seq`, `uptime_ms`, the readings, the optional
  `climate_sensor` (`sht31` or `dht11`, omitted when neither is found), the optional `rssi_dbm`
  and `fw`. Schema and example in `docs/contract/`. Retained `online`/`offline` status with a
  last will. Readings are dropped, not queued, while the broker is unreachable.
- **Setup.** A Wi-Fi setup portal (WiFiManager) on first boot, on a 3 s BOOT press or the
  `portal` command; Wi-Fi and MQTT settings are kept in NVS. MQTT username = `device_id`
  (default `olvar-` + the last 3 MAC bytes).
- **Link recovery.** Wi-Fi retries, a new MQTT session when a PUBACK is overdue, a full Wi-Fi
  reset after 100 s and a restart after 10 min without acknowledged telemetry. A broker name
  that stops resolving falls back to its last good address. A link record that survives a
  restart explains the previous boot's outage.
- **Serial console** (115200 baud): `status`, `raw`, `soilcheck` (guided air/water check of the
  soil probe with a verdict), `dhtwatch` (DHT11 breath test), `portal`, `reboot`, `help`.
- **Status LED.** Slow blink without Wi-Fi, fast blink without MQTT, a short flash per message.
- **Tooling.** Host tests for all of `lib/olvar_core` (`pio test -e native`), on-board tests,
  CI that runs the host tests, checks the contract fixture against its schema and builds the
  firmware, and `tools/sim_telemetry.sh` to feed the gateway without hardware.

### Known limits

- Firmware updates are over USB only (no OTA).
- MQTT is unencrypted on the LAN, with a password per device.
- Soil calibration lives in the dashboard; the firmware sends only `soil_raw`.
