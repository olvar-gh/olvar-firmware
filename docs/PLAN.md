# olvar-firmware — Project Plan

Status: planning only. No product code exists yet.
Scope: the ESP32 firmware that reads the plant sensors and publishes them over MQTT to the Raspberry Pi 3B gateway.
Research date: 2026-11-16. Every non-obvious claim links to its source.
**This plan follows the FINAL cross-repo contract decided by the user (§4).** That contract overrides the provisional one and any earlier choice in this file.

---

## 1. Summary

- **Toolchain:** PlatformIO Core 6.x with the **official `platformio/espressif32` platform pinned to `8.0.0`**. That release (2026-10-19) ships **Arduino-ESP32 core 3.3.12 on ESP-IDF 5.5.5**, so the community `pioarduino` fork is no longer needed. Board `esp32dev`, framework `arduino`, partition table `huge_app.csv` (firmware updates are over USB only, so there's no OTA slot).
- **Hardware (confirmed):** a 38-pin ESP32-WROOM-32D dev board on an expansion board with a 3.3 V/5 V jumper, plus a breadboard. Normal 2.4 GHz home Wi-Fi with no login page.
- **Libraries:** `RobTillaart/SHT31` (temperature/humidity), `claws/BH1750` (light), `bertmelis/espMqttClient` (MQTT 3.1.1, last will), `bblanchon/ArduinoJson` 7 (payload), `tzapu/WiFiManager` (first-time Wi-Fi + MQTT settings portal). `Preferences` (NVS) comes with the core.
- **Pins:** I2C bus on **GPIO21 (SDA) / GPIO22 (SCL)**, shared by SHT31 @ 0x44 and BH1750 @ 0x23, all at 3.3 V. Capacitive soil sensor on **GPIO34 (ADC1_CH6)**, because ADC2 can't be used while Wi-Fi is on. Add a **100 nF ceramic capacitor** from GPIO34 to GND.
- **Soil sensor:** check the board before use. It needs a **662K regulator**, a **TLC555** (not an NE555) and **R4 (1 MΩ) connected to GND**.
  - The firmware sends only `soil_raw`: the 12-bit ADC count, trimmed mean of 64 samples, higher = drier.
  - **The firmware has no calibration logic and doesn't send `soil_pct`.** Calibration lives in the database and is set from the dashboard's Soil calibration screen.
- **Telemetry:** every **60 s** on `olvar/v1/<device_id>/telemetry`, **QoS 1**.
  - Each boot gets a new random `boot_id`, and `seq` restarts at 0, so `(device_id, boot_id, seq)` deduplicates readings.
  - A failed sensor read gives `null`; `NaN` is never sent.
  - The status topic is a retained `online`/`offline` with a last will.
  - Readings are dropped, not queued, while the broker is unreachable.
- **Config and secrets:** nothing secret goes in git. Wi-Fi and MQTT settings are entered once in a captive portal and stored in NVS. MQTT is unencrypted on the LAN with a per-device username and password. For development, an optional gitignored `secrets.ini` supplies build-time defaults.
- **Power:** USB from a 5 V phone charger. **No deep sleep.**
- **Testing:** all logic goes in a hardware-free library `lib/olvar_core`, tested on the host with `pio test -e native` (Unity). Sensor and connectivity code are thin wrappers, checked by on-device tests (`pio test -e esp32dev`) and a bring-up checklist.

---

## 2. Research findings (with sources)

### 2.1 Toolchain and framework version
| Finding | Source |
|---|---|
| PlatformIO `espressif32` **8.0.0** was released 2026-10-19. It supports Arduino core **v3.3.12 (IDF v5.5.5)** and requires PlatformIO Core `^6`. | https://github.com/platformio/platform-espressif32/releases/tag/v8.0.0 , https://github.com/platformio/platform-espressif32/blob/v8.0.0/platform.json |
| PlatformIO officially announced Arduino Core v3 support in the official platform. Before this, core 3.x needed the `pioarduino` fork. | https://piolabs.com/blog/press-releases/esp32-arduino-v3-ai-development.html , https://github.com/sivar2311/platform-espressif32-versions |
| Arduino-ESP32 latest stable is **3.3.12** (2026-09-18). **4.0.0-RC1** is a pre-release only. | https://github.com/espressif/arduino-esp32/releases |
| 8.0.0 broke some projects: unpinned `platform = espressif32` silently upgraded them, and bigger core-3 binaries overflowed the default 1.25 MB app slot. `huge_app.csv` (3 MB app, no OTA) is the fix when OTA isn't needed. **Pin the exact version.** | https://medium.com/@displayengineer/platformio-espressif32-8-0-0-just-broke-a-pile-of-cyd-and-tft-builds-32665a59c7fa |
| The `esp32dev` board definition is 4 MB flash, 240 MHz. | https://github.com/platformio/platform-espressif32/blob/develop/boards/esp32dev.json |
| Fallback if 8.0.0 misbehaves: pioarduino `55.03.312-1` is the same core 3.3.12. | https://github.com/pioarduino/platform-espressif32/releases/tag/55.03.312-1 |

### 2.2 Pins, ADC and I2C on the ESP32-WROOM-32D
| Finding | Source |
|---|---|
| **ADC2 pins can't be used while Wi-Fi is on.** ADC1 is GPIO32–39. GPIO34–39 are input-only and have no internal pull-up or pull-down. GPIO0/2/5/12/15 are strapping pins. GPIO6–11 and 16–17 are wired to flash/PSRAM. | https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/gpio.html |
| `analogRead()` returns the raw 12-bit value (default resolution 12 bits). Measurable range on ESP32 at **11 dB attenuation is 150–3100 mV**. | https://docs.espressif.com/projects/arduino-esp32/en/latest/api/adc.html |
| At 11 dB the readings are accurate up to about 2.45 V. Above that they are non-linear and saturate at 4095 around 3.1 V. | https://solderhub.com/articles/esp32-adc-pins-analog-read-adc1-vs-adc2 |
| Espressif: the ADC is noise-sensitive. Use a **100 nF ceramic bypass capacitor on the ADC pad** plus **multisampling** (their graph uses 64 samples). | https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/adc_calibration.html |
| SHT31 address: **0x44** (ADDR low) or **0x45** (ADDR high). BH1750: **0x23** (ADDR low) or **0x5C** (ADDR high). They can share one 3.3 V bus. | https://esp32.co.uk/sht31-breakout-pinout-address-esp32-wiring-arduino , https://esp32.co.uk/bh1750-gy302-pinout-i2c-address-esp32-wiring-arduino |
| Sensirion: don't leave SHT3x ADDR floating. The chip accepts 2.15–5.5 V and I2C up to 1 MHz. | https://gaw.ru/components/sensors-ics/sht31/ (summarises the Sensirion SHT3x-DIS datasheet) |
| Adafruit SHT31-D breakout has 10 kΩ pull-ups on SDA/SCL and a 10 kΩ pull-down on ADR. | https://cdn-learn.adafruit.com/downloads/pdf/adafruit-sht31-d-temperature-and-humidity-sensor-breakout.pdf |
| GY-302 has its own SDA/SCL pull-ups. BH1750 ADDR goes high at ≥0.7×VCC, so tie it to **3V3, never 5 V**. A high-res reading takes 120 ms. | https://www.tinkered.ai/components/bh1750/esp32 |
| 38-pin ESP32 expansion boards have a **jumper that puts 3.3 V or 5 V on every V pin of the G-V-S headers**. It must stay on 3.3 V for ESP32 sensors. | https://protosupplies.com/product/esp32-s-expansion-board-38-pin , https://osoyoo.com/2025/02/07/osoyoo-breakout-board-for-38p-esp32/ |
| Sensirion: heat from nearby electronics raises temperature and lowers RH. At 90 %RH, 1 °C of error is about 5 %RH. **Keep the sensor away from heat sources.** | https://sensirion.com/media/documents/FC5BED84/662B494D/Sensirion_Humidity_Temperature_Design_Guide.pdf |
| The ESP32 radio is 2.4 GHz only. | https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf |

### 2.3 Capacitive soil moisture sensor ("v1.2"/"v2.0") hardware flaws
| Finding | Source |
|---|---|
| Three common factory defects. (1) The 662K 3.0 V regulator is missing (pads bridged), so the output follows the supply voltage. (2) An **NE555** is fitted instead of a TLC555; it needs about 4.5 V, so it fails or misbehaves at 3.3 V. (3) **R4 (1 MΩ) is not connected to GND**, so the sensor responds very slowly or returns stale values. | https://github.com/OrangePeachPink/sprout/blob/main/docs/SENSOR_QA.md (cites the Flaura teardown) |
| The missing R4 ground path is confirmed in a peer-reviewed characterisation. | https://pmc.ncbi.nlm.nih.gov/articles/PMC7348898 |
| Fix for (3): solder a 1 MΩ resistor between AOUT and GND. Fix for (2): use ≥4.5 V or swap the chip. The PCB edges are not sealed, so moisture gets in. Coat the edges and the electronics. | https://arduinodiy.wordpress.com/2020/08/24/soil-moisture-sensors |
| Test without power. Read the 662K and TLC555 markings, then meter AOUT↔GND, which should be about 1 MΩ. The printed version number doesn't tell you the quality. | https://github.com/OrangePeachPink/sprout/blob/main/docs/user/trust-your-sensor.md |
| The LDO needs headroom. Some boards combine the LDO with an NE555, and those can't work at all. v2.0 boards can be noisy. | https://miltschek.de/article_2022-06-26_Soil+Moisture+Sensors.html |
| Output is **inverse**: dry gives a high voltage, wet a low one. At 3.3 V, typical readings are about 3.0 V in air and about 1.5 V in water. Calibrate every unit. Insert only up to the marked line. | https://tinkered.ai/components/capacitive-soil-moisture-v12 |

### 2.4 Libraries
| Finding | Source |
|---|---|
| `RobTillaart/SHT31` 0.5.3 (2026-01-23), MIT, no extra dependencies. | https://github.com/RobTillaart/SHT31 , https://www.arduinolibraries.info/libraries/sht31 |
| Adafruit SHT31 library: last release 2.2.2 in 2023, needs BusIO. Sensirion official `arduino-i2c-sht3x` needs Sensirion Core. | https://www.arduinolibraries.info/libraries/adafruit-sht31-library , https://github.com/Sensirion/arduino-i2c-sht3x |
| `claws/BH1750` 1.3.0. The user code must call `Wire.begin()`. Address is set in `begin()`. | https://github.com/claws/BH1750 , https://www.tinkered.ai/components/bh1750/esp32 |
| **PubSubClient is officially unmaintained** (author's notice, 2026). | https://github.com/knolleary/pubsubclient |
| `espMqttClient` 1.7.3. MQTT 3.1.1, all QoS levels, `setWill()`, outbox for QoS 1/2. On ESP32 it runs its own task. Builds on Linux for tests. | https://github.com/bertmelis/espMqttClient , https://github.com/bertmelis/espMqttClient/blob/main/docs/index.md |
| espMqttClient had reconnect-stall bugs in old versions (fixed 1.4.x). Reconnection is the app's job: call `connect()` again after `onDisconnect`. | https://github.com/bertmelis/espMqttClient/issues/102 , https://github.com/bertmelis/espMqttClient/releases |
| ArduinoJson 7.4.3 (2026-03-02). Builds on any C++11 compiler, so it works in host tests. | https://arduinojson.org/v7/revisions/ |
| `tzapu/WiFiManager` 2.0.17. Captive portal, custom parameters, ESP32 support. | https://docs.arduino.cc/libraries/wifimanager , https://solderhub.com/articles/esp32-wifimanager-captive-portal-no-hardcoded-wifi |
| `Preferences` stores key-values in NVS. Namespace and key names are limited to **15 characters**. | https://docs.espressif.com/projects/arduino-esp32/en/latest/tutorials/preferences.html |

### 2.5 Wi-Fi and MQTT broker
| Finding | Source |
|---|---|
| `WiFi.setAutoReconnect()` and `WiFi.onEvent()`; disconnect events carry a reason code. Event callbacks run in another FreeRTOS task. | https://docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html |
| Layered pattern: auto-reconnect, a disconnect-reason log, and a watchdog that reboots if the link is down longer than N minutes. | https://8bitlabs.ca/Posts/2026/06/25/esp32-wifi |
| **Mosquitto 2.x needs explicit authentication.** Once a listener is configured, `allow_anonymous` defaults to false. Use `mosquitto_passwd` to create users. | https://mosquitto.org/documentation/migrating-to-2-0 , https://mosquitto.org/documentation/authentication-methods |
| Arduino network clients can't resolve `*.local` names by themselves. On ESP32, `MDNS.queryHost()` returns an `IPAddress`. | https://github.com/256dpi/arduino-mqtt , https://github.com/espressif/arduino-esp32/issues/10613 |

### 2.6 Testing
| Finding | Source |
|---|---|
| PlatformIO Test Runner supports **native** (host) and **embedded** (on-board) tests. Native needs a system GCC/Clang on PATH. | https://docs.platformio.org/en/stable/advanced/unit-testing/runner.html |
| Tests live in `test/test_*` folders. `test_filter`/`test_ignore` split them per environment. Code under test should be in `lib/`. | http://docs.platformio.org/en/latest/advanced/unit-testing/structure/hierarchy.html , https://xa1.at/platformio-unit-test |
| Unity is the default framework and runs both native and embedded. | http://docs.platformio.org/en/latest/advanced/unit-testing/frameworks/unity.html |

### 2.7 Outside firmware (checked because the contract mentions it)
| Finding | Source |
|---|---|
| `deepseek-flash` is a real DeepSeek API model id (currently DeepSeek-V4.1-Flash-1017; thinking can be switched off; JSON Output supported). `deepseek-chat`/`deepseek-reasoner` were retired 2026-07-24. Price per 1M tokens, off-peak: $0.15 input (cache miss), $0.60 output; peak hours cost double. | https://api-docs.deepseek.com/quick_start/pricing , https://api-docs.deepseek.com/updates |

The chat Edge Function belongs to olvar-gateway and has no firmware impact.

---

## 3. Decisions and alternatives rejected

| Topic | Decision | Rejected alternatives and why |
|---|---|---|
| Build system | **PlatformIO Core 6.x CLI** (VS Code extension optional). `platformio.ini` pins everything. | **Arduino CLI**: no built-in host unit-test runner, and library pinning is weaker. **ESP-IDF directly**: far more code, and the sensor libraries are Arduino-based. **pioarduino fork**: no longer needed. Kept as the fallback. |
| Platform / core | `platform = espressif32 @ 8.0.0` (exact pin), Arduino core 3.3.12 | `~8.0.0`: auto-upgrades patch versions. Core 4.0.0-RC1: pre-release only. |
| Partition table | `board_build.partitions = huge_app.csv` (3 MB app, no OTA slot) | `default.csv`: 1.25 MB slots may overflow. `min_spiffs.csv`: only useful for OTA, which is out of v1 (decision F). |
| Firmware updates | **USB only** (decision F) | ArduinoOTA / HTTP OTA: out of v1 scope. |
| SHT31 driver | `robtillaart/SHT31 @ 0.5.3`: single-shot read, CRC checked, heater off | Adafruit: older, pulls in BusIO. Sensirion official: second choice. |
| BH1750 driver | `claws/BH1750 @ 1.3.0`, `ONE_TIME_HIGH_RES_MODE` | `hp_BH1750`: fine, less common. Continuous mode: no benefit at 60 s. |
| MQTT client | `bertmelis/espMqttClient @ 1.7.3`, sync client over `WiFiClient` | **PubSubClient**: unmaintained. **arduino-mqtt**: blocking `loop()`, small buffers. **ESP32MQTTClient**: hides reconnect logic. |
| JSON | `bblanchon/ArduinoJson @ 7.4.3` | Hand-written `snprintf`: error-prone with `null`. |
| Provisioning | **WiFiManager captive portal** on first boot, or when BOOT (GPIO0) is long-pressed (≥3 s) **while the firmware is running**. The long-press sets an NVS flag and reboots into the portal. Holding GPIO0 low *during reset* would enter ROM download mode instead, because GPIO0 is a strapping pin. Portal fields: MQTT host, port, user, password, device_id. | Hard-coded `secrets.h`: credentials end up in the binary and need a rebuild for every change (kept only as gitignored dev defaults). Espressif WiFiProv/BLE: needs the Espressif app and can't collect MQTT fields. |
| Settings storage | `Preferences` namespace `olvar`. Wi-Fi credentials stay in the Wi-Fi driver's own NVS store, written by WiFiManager. | LittleFS JSON: more code. EEPROM emulation: legacy. |
| Broker address | Host string; `.local` names resolve via `MDNS.queryHost()`. **DHCP reservation for the Pi recommended.** | mDNS only: unreliable on some routers. |
| MQTT security | LAN, port 1883, username+password per device, **no TLS** (decision F) | TLS: out of v1. |
| Timestamps / buffering | No wall-clock time on the device; the gateway stamps on receipt. **Readings are dropped while the broker is unreachable** (decision C). | ESP32 ring buffer + replay: replayed readings would get wrong gateway stamps. |
| Telemetry QoS | **QoS 1, not retained** (decision C). On disconnect the firmware discards any unacknowledged telemetry rather than resending it later, so a late resend can't get a stale gateway timestamp. T6 must verify espMqttClient's outbox behaviour and clear it on disconnect if needed. | QoS 0: rejected by the final contract. |
| `boot_id` | 8 lowercase hex chars from `esp_random()`, generated once per boot after Wi-Fi starts (hardware RNG) | MAC- or counter-based ids: not random; an NVS boot counter wears flash for no benefit. |
| Soil sampling | Sensor powered from 3V3 all the time. 64 samples ~1 ms apart: sort, drop the lowest and highest 16, average the middle 32. Report the 12-bit count. 100 nF on the pin. | Power from a GPIO: a board with the R4 defect then needs long settling. `analogReadMilliVolts`: the contract field is a raw count. |
| Soil calibration | **None in firmware** (decision D). Dry/wet values live in the database and are set from the dashboard. | Serial `cal` commands / NVS calibration (earlier draft): removed. |
| Interval | Fixed **60 s** compile-time constant (`OLVAR_INTERVAL_S`), not a portal field (decision E) | Configurable interval: not needed for v1. |
| Power | USB 5 V from a ≥1 A charger. **No deep sleep.** Default Wi-Fi modem-sleep. | Deep sleep: the MQTT connection and retained status would flap on every wake, for no gain on USB. |
| Watchdogs | No successful publish for 10 min → `ESP.restart()`, plus the core's task watchdog | Restart on any Wi-Fi drop: too aggressive. |

---

## 4. Contract changes

The user's **FINAL contract** replaces the provisional one. Below is the firmware-facing part as the firmware will implement it, then what changed relative to the provisional contract and my first draft. Nothing is still open on the firmware side.

### 4.1 Final firmware contract
- **Transport:** Wi-Fi to the Mosquitto broker on the Pi 3B, port 1883, unencrypted on the LAN, per-device username/password.
- **Telemetry topic** `olvar/v1/<device_id>/telemetry`, **QoS 1**, not retained, every **60 s**.
  ```json
  {"device_id":"olvar-a1b2c3","boot_id":"9f3c07ab","seq":42,"uptime_ms":2520311,
   "temperature_c":23.41,"humidity_pct":51.07,"lux":312.5,"soil_raw":2210,
   "rssi_dbm":-61,"fw":"0.1.0+a1b2c3d"}
  ```
  | Field | Type | Firmware rule |
  |---|---|---|
  | `device_id` | string | default `olvar-` + last 3 MAC bytes as 6 lowercase hex; overridable in the portal; `[a-z0-9-]{1,32}`; equals the topic segment and the MQTT client id |
  | `boot_id` | string | 8 lowercase hex chars, random per boot |
  | `seq` | uint32 | 0 at boot, +1 per telemetry message |
  | `uptime_ms` | uint32 | `millis()`, wraps after ~49.7 days |
  | `temperature_c`, `humidity_pct` | number \| null | 2 dp; `null` on read/CRC failure |
  | `lux` | number \| null | 1 dp; `null` on failure |
  | `soil_raw` | int \| null | 12-bit ADC count (0–4095) at 11 dB, trimmed mean of 64 samples; **higher = drier**; `null` if the sampling fails |
  | `rssi_dbm` | int | optional; omitted when not connected |
  | `fw` | string | SemVer + git short hash, e.g. `0.1.0+a1b2c3d` |
- **NaN or Inf is never sent.** Any non-finite float is serialised as `null`. **No `soil_pct` is sent.**
- `(device_id, boot_id, seq)` is the dedup key. The gateway ignores unknown fields.
- **Status topic** `olvar/v1/<device_id>/status`: plain string `online`/`offline`, retained, QoS 1. Last will: `offline`, retained. The firmware publishes `online` on every connect and `offline` before a deliberate restart (portal or watchdog).
- **No device-side buffering:** while the broker is unreachable, readings are dropped.

### 4.2 What changed
| Item | Provisional contract / my first draft | Final |
|---|---|---|
| Telemetry QoS | my draft: QoS 0 | **QoS 1**, unacked telemetry discarded on disconnect |
| `boot_id` | absent | **added**, 8 hex, random per boot |
| `soil_pct` | sent by firmware, calibration in NVS via serial `cal` | **removed from firmware**; calibration in the DB via the dashboard; `soil_pct` computed server-side |
| Nullable fields, no NaN | my proposal | **adopted** |
| `rssi_dbm` optional, unknown fields ignored | my proposal | **adopted** |
| Dedup key | my draft: `(device_id, seq, uptime_ms)` | `(device_id, boot_id, seq)` |
| Interval | configurable 10–3600 s | **fixed 60 s** |
| OTA | optional ArduinoOTA | **removed**; USB only |

---

## 5. Architecture and folder layout

### 5.1 Runtime design (single Arduino `loop()`, no blocking waits)
```
boot ─► make boot_id ─► load Settings (NVS) ─► if no Wi-Fi creds OR portal flag ─► WiFiManager portal ─► save ─► restart
     └► Wi-Fi STA (autoReconnect, event log) ─► MQTT (espMqttClient, LWT) ─► publish status "online" (retained)
loop:  serial CLI poll │ BOOT long-press check │ every 60 s:
         read SHT31 ─► read BH1750 ─► sample soil ─► build Reading (nullable fields)
         ─► if MQTT connected: publish telemetry (QoS 1), seq++ ; else: drop, count skipped
       on MQTT disconnect: discard unacked telemetry; reconnect with backoff (1 s → 60 s, ±20 % jitter)
       supervisor: no successful publish for 10 min → publish "offline" (if possible) → restart
       status LED: slow blink = no Wi-Fi, fast blink = no MQTT, short flash = published
```

### 5.2 Layering
- `lib/olvar_core/`: **pure C++17, no `Arduino.h`**. Holds the ADC filter, the float sanitiser, the telemetry JSON builder, the topic builder and `device_id`/`boot_id` formatting, settings validation, the CLI parser and the backoff/interval timer. Depends only on ArduinoJson.
- `src/`: thin Arduino adapters (drivers, Wi-Fi, MQTT, NVS, LED, serial) and `main.cpp`.

### 5.3 Folder layout (target)
```
olvar-firmware/
├── platformio.ini              # envs: esp32dev (firmware), native (host tests); extra_configs = secrets.ini
├── secrets.example.ini         # committed template; secrets.ini is gitignored
├── .gitignore                  # .pio/, secrets.ini, .vscode/ (except extensions.json)
├── README.md                   # quick start: install, flash, provision
├── .github/workflows/ci.yml    # pio test -e native ; pio run -e esp32dev
├── scripts/version.py          # appends git short hash to the fw string
├── docs/
│   ├── PLAN.md                 # this file
│   ├── WIRING.md               # wiring table + soil-sensor check (from §7)
│   ├── PROVISIONING.md         # first-time setup, portal fields, re-opening the portal
│   ├── BRINGUP.md              # hardware bring-up checklist
│   └── contract/
│       ├── telemetry.schema.json
│       └── telemetry.example.json
├── include/
│   └── pins.h                  # GPIO numbers + I2C addresses + OLVAR_INTERVAL_S
├── lib/olvar_core/
│   ├── library.json
│   └── src/
│       ├── reading.h                   # Reading/Meta structs (T0)
│       ├── adc_filter.h / .cpp         # trimmedMean()
│       ├── sanitize.h / .cpp           # finite-or-null, rounding
│       ├── telemetry.h / .cpp          # buildTelemetryJson()
│       ├── ids.h / ids.cpp             # topics, isValidDeviceId, deviceIdFromMac, bootIdFromRandom
│       ├── settings.h / .cpp           # struct Settings, defaults, validate()
│       ├── cli.h / .cpp                # parseCommand()
│       └── backoff.h / .cpp            # Backoff, IntervalTimer
├── src/
│   ├── main.cpp
│   ├── app.h / app.cpp
│   ├── sensors/ sht31_sensor.* bh1750_sensor.* soil_sensor.* i2c_bus.*
│   ├── net/ wifi_link.* mqtt_link.* provisioning.*
│   ├── storage/settings_store.*
│   ├── serial_cli.*
│   └── status_led.*
└── test/
    ├── native/   test_adc_filter/ test_sanitize/ test_telemetry/ test_ids/ test_settings/ test_cli/ test_backoff/
    └── embedded/ test_i2c_bus/ test_sensors/
```

### 5.4 `platformio.ini` sketch (for T0; versions pinned)
```ini
[platformio]
default_envs = esp32dev
extra_configs = secrets.ini        ; optional, gitignored

[env]
build_flags = -std=gnu++17 -Wall -DOLVAR_FW_VERSION=\"0.1.0\"
lib_deps = bblanchon/ArduinoJson @ 7.4.3

[env:esp32dev]
platform = espressif32 @ 8.0.0
board = esp32dev
framework = arduino
monitor_speed = 115200
monitor_filters = esp32_exception_decoder
board_build.partitions = huge_app.csv
lib_deps =
  ${env.lib_deps}
  robtillaart/SHT31 @ 0.5.3
  claws/BH1750 @ 1.3.0
  bertmelis/espMqttClient @ 1.7.3
  tzapu/WiFiManager @ 2.0.17
test_filter = embedded/*

[env:native]
platform = native
test_framework = unity
test_filter = native/*
```
T0 must confirm that `extra_configs` tolerates a missing `secrets.ini`. If it doesn't, T0 uses a pre-script that creates an empty one.

---

## 6. Task breakdown

**Ordering:** T0 runs first and alone. After that, T1–T7 own disjoint files and can run in parallel. T8 integrates them last.

**Shared interfaces:** the signatures below are binding so that parallel workers agree. A worker who needs a change appends it to `.kitchen/notes.md`.

```cpp
// lib/olvar_core/src/reading.h  (created by T0, read-only for others)
struct OptF { bool ok=false; float v=0; };   struct OptI { bool ok=false; int32_t v=0; };
struct Reading { OptF temperature_c, humidity_pct, lux; OptI soil_raw; };
struct Meta    { const char* device_id; const char* boot_id; uint32_t seq; uint32_t uptime_ms; const char* fw; OptI rssi_dbm; };
// adc_filter.h int32_t trimmedMean(int32_t* samples, size_t n, size_t trimEachSide);   // sorts in place
// sanitize.h   OptF finiteOrNull(float v); float roundTo(float v, int decimals);
// telemetry.h  size_t buildTelemetryJson(const Reading&, const Meta&, char* out, size_t cap); // 0 on overflow
// ids.h        bool isValidDeviceId(const char*); void deviceIdFromMac(const uint8_t mac[6], char out[13]);
//              void bootIdFromRandom(uint32_t r, char out[9]);
//              size_t telemetryTopic(const char* id, char* out, size_t cap); size_t statusTopic(const char* id, char* out, size_t cap);
// settings.h   struct Settings{char device_id[33]; char mqtt_host[64]; uint16_t mqtt_port=1883; char mqtt_user[33]; char mqtt_pass[65];};
//              bool validate(const Settings&, const char** err);
// cli.h        enum class Cmd{Status,Raw,Portal,Reboot,Help,Unknown}; Cmd parseCommand(const char* line);
// backoff.h    class Backoff{ public: Backoff(uint32_t minMs,uint32_t maxMs); uint32_t next(uint32_t rand01k); void reset(); };
//              class IntervalTimer{ public: explicit IntervalTimer(uint32_t periodMs); bool due(uint32_t nowMs); };
```

### T0 — Scaffold (first, alone)
**Owns:** `platformio.ini`, `secrets.example.ini`, `.gitignore`, `README.md`, `include/pins.h`, `lib/olvar_core/library.json`, `lib/olvar_core/src/reading.h`, `src/main.cpp` (stub that prints the version), `scripts/version.py`, `.github/workflows/ci.yml`, `test/native/test_smoke/`.
**Acceptance:**
- `pio run -e esp32dev` builds with `huge_app.csv`.
- `pio test -e native` passes the smoke test.
- CI runs both.
- `secrets.ini` is gitignored and the build passes without it.
- `pins.h` defines SDA=21, SCL=22, SOIL=34, LED=2, BOOT=0, SHT31_ADDR=0x44, BH1750_ADDR=0x23, `OLVAR_INTERVAL_S`=60.

**Verify:** run both commands locally and on CI. No hardware needed.

### T1 — ADC filter and sanitiser (olvar_core)
**Owns:** `lib/olvar_core/src/adc_filter.*`, `lib/olvar_core/src/sanitize.*`, `test/native/test_adc_filter/`, `test/native/test_sanitize/`.
**Acceptance:**
- `trimmedMean` with n=64, trim=16 gives the mean of the middle 32 and ignores outliers. n=1 and trim≥n/2 fall back to the median.
- `finiteOrNull` returns `ok=false` for NaN and ±Inf.
- `roundTo` rounds half away from zero.

**Verify:** `pio test -e native -f native/test_adc_filter -f native/test_sanitize`. No hardware.

### T2 — Telemetry JSON, ids/topics, contract fixtures (olvar_core)
**Owns:** `lib/olvar_core/src/telemetry.*`, `lib/olvar_core/src/ids.*`, `test/native/test_telemetry/`, `test/native/test_ids/`, `docs/contract/telemetry.schema.json`, `docs/contract/telemetry.example.json`.
**Acceptance:**
- The output has exactly the §4.1 keys, plus `rssi_dbm` only when `ok`. **No `soil_pct` key.**
- A field with `ok=false`, NaN or Inf serialises as `null`.
- Decimal places per §4.1.
- Returns 0 when the buffer is too small (no truncated JSON).
- The test parses the produced JSON back with ArduinoJson and checks it against the example.
- Topics are `olvar/v1/<id>/telemetry` and `olvar/v1/<id>/status`.
- `isValidDeviceId` accepts `[a-z0-9-]{1,32}` and rejects `/`, `+`, `#`, upper case, empty.
- `deviceIdFromMac` gives `olvar-` + the last 3 bytes in lowercase hex.
- `bootIdFromRandom` gives exactly 8 lowercase hex chars, zero-padded.

**Verify:** native tests. Optionally `npx ajv-cli validate -s docs/contract/telemetry.schema.json -d docs/contract/telemetry.example.json`. No hardware. Post the fixture path in notes for the gateway worker.

### T3 — Settings model, CLI parser, backoff/timer (olvar_core)
**Owns:** `lib/olvar_core/src/settings.*`, `lib/olvar_core/src/cli.*`, `lib/olvar_core/src/backoff.*`, `test/native/test_settings/`, `test/native/test_cli/`, `test/native/test_backoff/`.
**Acceptance:**
- `validate` rejects: empty host, port 0, invalid device_id.
- The parser is case-insensitive, trims whitespace, and maps unknown input to `Unknown`.
- `Backoff` doubles from min to max, applies ±20 % jitter, and `reset()` returns it to min.
- `IntervalTimer` handles `millis()` wraparound (0xFFFFFF00 → 0x00000100).

**Verify:** native tests. No hardware.

### T4 — Sensor drivers and wiring doc
**Owns:** `src/sensors/*`, `test/embedded/test_i2c_bus/`, `test/embedded/test_sensors/`, `docs/WIRING.md`.
**Acceptance:**
- `I2cBus` runs Wire at 100 kHz on GPIO21/22 and provides `scan()`.
- Bus recovery: after 3 consecutive failures it toggles SCL 9× and runs `Wire.end()/begin()`.
- `Sht31Sensor::read()` does a single-shot, high-repeatability, CRC-checked read; a failure gives `ok=false`.
- `Bh1750Sensor::read()` uses one-time high-res mode; a negative result gives `ok=false`.
- `SoilSensor::sampleRaw()` sets 11 dB attenuation on GPIO34, takes 64 samples ~1 ms apart, then `trimmedMean(…,16)`.
- No call blocks longer than 200 ms.
- `WIRING.md` contains §7.3–§7.4.

**Verify on hardware:** `pio test -e esp32dev -f embedded/*`.
- The scan finds 0x44 and 0x23.
- Temperature is 5–40 °C, RH 10–95 %.
- Lux is >0 under room light and ≤5 when covered.
- `soil_raw` differs by >500 counts between air and a glass of water.

**Without hardware:** compile only (`pio run -e esp32dev`).

### T5 — Settings storage and provisioning
**Owns:** `src/storage/settings_store.*`, `src/net/provisioning.*`, `docs/PROVISIONING.md`.
**Acceptance:**
- `SettingsStore` load/save/clear round-trips `Settings` in Preferences namespace `olvar`. Keys: `dev_id, mq_host, mq_port, mq_user, mq_pass, portal`.
- Missing keys fall back to defaults, then to `secrets.ini` build flags (`OLVAR_DEFAULT_*`).
- The portal starts if there are no Wi-Fi credentials or the `portal` flag is set. The flag is set by a ≥3 s BOOT long-press at runtime (non-blocking detector owned here) or by the `portal` CLI command; GPIO0 is never read during reset.
- AP `olvar-setup-<mac6>`; its password is printed on serial.
- Fields are pre-filled from the current settings and validated before saving.
- The portal times out after 5 min and reboots into normal mode if valid settings exist.

**Verify on hardware:**
- Erase flash (`pio run -t erase`), flash, provision from a phone; the board reboots and joins Wi-Fi.
- Power-cycle; the settings persist.
- Long-press BOOT while running; the portal reopens.

**Without hardware:** compile only.

### T6 — Wi-Fi and MQTT link
**Owns:** `src/net/wifi_link.*`, `src/net/mqtt_link.*`.
**Acceptance:**
- STA mode with `setAutoReconnect(true)`, hostname = device_id, and disconnect reasons logged.
- `MqttLink` sets client id = device_id, clean session, credentials, keepalive 30 s, and the will (`status`, `offline`, QoS 1, retain). On connect it publishes retained `online`.
- `publishTelemetry()` publishes at QoS 1 and returns false when not connected; it never queues while disconnected.
- **On disconnect, any unacked telemetry is discarded.** Check whether espMqttClient keeps its outbox across reconnects; if it does, clear it. Document the result in the code.
- Reconnect uses `Backoff(1000, 60000)` from `loop()`, never from a callback.
- `.local` hosts are resolved via `MDNS.queryHost`.
- `goOffline()` publishes `offline` (≤500 ms) before a deliberate restart.
- Supervisor: no successful publish for 10 min → restart.

**Verify on hardware + Pi:**
- `mosquitto_sub -h <pi> -u <user> -P <pw> -v -q 1 -t 'olvar/v1/#'` shows `online` (retained) and telemetry every 60 s with `boot_id` constant and `seq` increasing.
- Unplug the ESP32. Within about 1.5× keepalive, status changes to `offline`.
- Stop Mosquitto for 2 min, then start it again. The device reconnects without a reboot, `seq` jumps (dropped readings), and no old reading arrives late.
- After a reset, `boot_id` changes and `seq` restarts at 0.

**Without hardware:** compile only. Optional local host test against `mosquitto` (espMqttClient builds on Linux); not in CI.

### T7 — Serial CLI and status LED
**Owns:** `src/serial_cli.*`, `src/status_led.*`.
**Acceptance:**
- The CLI reads lines at 115200. `status` prints IP, RSSI, MQTT state, device_id, boot_id, seq and free heap. `raw` prints one fresh `soil_raw` plus the SHT31/BH1750 values, for wiring checks. `portal` sets the flag and reboots. `reboot`, `help`.
- LED patterns as in §5.1, all non-blocking.
- No calibration commands.

**Verify:** on hardware. Without hardware: compile only; parsing is already covered by T3.

### T8 — Integration, bring-up, release 0.1.0 (last)
**Owns:** `src/main.cpp`, `src/app.*`, `docs/BRINGUP.md`, version bump.
**Acceptance:**
- Implements §5.1 using T1–T7.
- The first telemetry is sent ≤5 s after the MQTT connection, then every 60 s.
- One sensor unplugged → that field is `null` and the others are still sent.
- No NaN ever appears on the wire.
- Free heap stays steady over a 24 h soak.
- The full native test suite passes and CI is green.

**Verify:**
- 24 h soak with the Pi, logging `mosquitto_sub` to a file. Expect ≥99 % of the expected 1440 messages, no unplanned reboot (`boot_id` constant) and no duplicate `(boot_id, seq)`.
- Unplug the SHT31 VIN mid-run and check for `null` and then recovery.
- Cross-repo: the gateway inserts the messages, and the dashboard calibration screen shows live `soil_raw`.

### What can be tested without hardware
- All of `olvar_core` (T1–T3) with native Unity tests in CI.
- A compile check of every Arduino adapter, also in CI.
- The contract fixtures (T2), which the gateway can reuse.
- The **gateway** can be developed without the ESP32 by publishing `docs/contract/telemetry.example.json` with `mosquitto_pub -q 1` in a loop.

---

## 7. Things the user must do by hand

### 7.1 Installs (your Mac)
1. Install PlatformIO Core: `brew install platformio`, `pipx install platformio`, or the VS Code "PlatformIO IDE" extension. Native tests also need `xcode-select --install`. Guide: https://docs.platformio.org/en/latest/core/installation/index.html
2. Connect the ESP32 with a **data** USB cable. If no `/dev/cu.*` port appears, install the USB-UART driver for the chip near the USB socket (CP2102 or CH340).

### 7.2 Raspberry Pi 3B (details in the gateway plan; the firmware needs these)
1. Give the Pi a **DHCP reservation** in your router and note its IP.
2. Install Mosquitto. Create one user for the device: `sudo mosquitto_passwd /etc/mosquitto/passwd olvar-a1b2c3`, using the device id the ESP32 prints on its first boot.

### 7.3 Check the soil sensor before wiring (5 min, sensor unplugged)
1. The small 3-pin chip near the connector should say **662K**.
2. The 8-pin chip must say **TLC555** (or "TL555"/"555C"). **NE555 won't work at 3.3 V**: replace the sensor.
3. Multimeter on resistance, **AOUT ↔ GND**: about **1 MΩ** is good. Open/"OL" means solder a 1 MΩ resistor between AOUT and GND.
4. Coat the PCB edges and the top electronics (nail varnish, PlastiDip or heat-shrink). **Insert only up to the white line.**

### 7.4 Wiring table (38-pin ESP32-WROOM-32D on the expansion board + breadboard)

**First:** set the expansion board's **V-rail jumper to 3.3 V**. Make two breadboard rows for SDA and SCL so both I2C sensors share them.

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
| 11 | Soil sensor **VCC** (red) | **3V3** | |
| 12 | Soil sensor **GND** (black) | **GND** | |
| 13 | Soil sensor **AOUT** (yellow) | **GPIO34** | ADC1_CH6, input-only; **not** an ADC2 pin |
| 14 | 100 nF ceramic capacitor ("104") | between **GPIO34** and **GND**, close to the ESP32 | ADC noise |
| 15 | USB cable | ESP32 board's USB port → 5 V ≥1 A charger (or your Mac while flashing) | |

**Pins not to use for sensors:** GPIO0/2/5/12/15 (boot strapping), GPIO6–11 and 16–17 (flash), and all ADC2 pins for analog.

**Placement:** put the SHT31 on 15–20 cm wires **away from the ESP32 and the charger**, out of direct sun. Point the GY-302 window up at plant level, uncovered. Keep I2C wires ≤30 cm.

### 7.5 First boot and provisioning
1. `pio run -t upload && pio device monitor`. The serial monitor prints the device id and the setup AP password.
2. On your phone, join **`olvar-setup-xxxxxx`**. The portal opens (if not, browse to 192.168.4.1).
3. Choose your home 2.4 GHz network and enter its password. Enter the MQTT host (the Pi's IP), port 1883, and the MQTT user and password from §7.2. Save.
4. On the Pi, `mosquitto_sub -h localhost -u <admin> -P <pw> -v -t 'olvar/v1/#'` should show `online` and then telemetry every 60 s.
5. To change settings later: while the board is running, hold **BOOT** for 3 s, then release. It reboots into the portal. Don't hold BOOT while pressing EN/RST: that enters flashing mode.

### 7.6 Soil calibration (done in the dashboard, not the firmware)
Calibrate with the dashboard's **Soil calibration** screen, which reads live `soil_raw`. Readings arrive only every 60 s, so allow time:
1. Hold the dry, wiped sensor in the air for **at least 2 minutes** (2 readings), then save "dry".
2. Put it in a glass of water up to the line for at least 2 minutes, then save "wet".

Higher raw means drier. Typical values are about 2600–3300 dry and 1200–1800 wet.

### 7.7 Firmware updates
USB only: plug in the Mac and run `pio run -t upload`. Settings in NVS survive an upload. Only `pio run -t erase` clears them.

---

## 8. Risks and open questions for the user

### Open questions
The final decisions answer the earlier questions (board, Wi-Fi, OTA, soil scale, interval). **No firmware question is still open.** One hardware fact is still unknown until you check it: whether your soil sensor passes §7.3. If it has an NE555 or fails the 1 MΩ test, fix or replace it before T4's hardware verification.

### Risks
| Risk | Likelihood | Mitigation |
|---|---|---|
| Faulty cheap soil sensor (NE555 / floating R4 / missing regulator) | High | §7.3 check, 1 MΩ fix, replace if NE555 |
| Soil sensor drifts over months as moisture gets into its edges | Medium | Coat the edges. Re-calibrate in the dashboard. `soil_raw` is always stored. |
| Dry-air reading sits near the ADC's non-linear top (>2.45 V at 11 dB) | Medium | The server-side two-point calibration absorbs it. Readings only clip at 4095 (~3.1 V). |
| Calibrating at a 60 s interval is slow: one reading per minute | Medium | §7.6 says to wait ≥2 readings. The dashboard should show the age of the latest `soil_raw`. |
| Temperature reads high because the ESP32 or charger warms the SHT31 | Medium | Place it 15–20 cm away. |
| I2C errors from breadboard jumper wires | Medium | 100 kHz, short wires, bus recovery, `null` fields |
| QoS 1 resend after reconnect would arrive late and get a stale gateway timestamp | Low | Discard unacked telemetry on disconnect (T6). The gateway also dedupes on `(device_id, boot_id, seq)`. |
| PlatformIO 8.0.0 is only 4 weeks old | Low–Medium | Exact pin. Fallback to pioarduino `55.03.312-1` (same core). |
| Unencrypted MQTT on the LAN (accepted in decision F) | Low (home LAN) | Per-device credentials; broker not exposed outside the LAN |
| USB power from the Pi browns out the ESP32 | Low | Use a separate ≥1 A charger. |
| espMqttClient reconnect edge cases | Low | App-level backoff plus the 10-minute no-publish restart |
