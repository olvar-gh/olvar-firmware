# olvar-firmware — Project Plan

Status: research and planning only. No product code exists yet.
Scope: the ESP32 firmware that reads the plant sensors and publishes them over MQTT to the Raspberry Pi gateway.
Research date: 2026-11-16. Every non-obvious claim links to its source.

---

## 1. Summary

- **Toolchain:** PlatformIO Core 6.x with the **official `platformio/espressif32` platform pinned to `8.0.0`**. That release (2026-10-19) ships **Arduino-ESP32 core 3.3.12 on ESP-IDF 5.5.5**, so the community `pioarduino` fork is no longer needed. Board `esp32dev`, framework `arduino`.
- **Libraries:** `RobTillaart/SHT31` (temperature/humidity), `claws/BH1750` (light), `bertmelis/espMqttClient` (MQTT 3.1.1, last will, auto-managed connection task), `bblanchon/ArduinoJson` 7 (payload), `tzapu/WiFiManager` (first-time Wi-Fi + settings portal). `Preferences` (NVS) and `ArduinoOTA` come with the core.
- **Pins:** I2C bus on **GPIO21 (SDA) / GPIO22 (SCL)**, shared by SHT31 @ 0x44 and BH1750 @ 0x23, all at 3.3 V. Capacitive soil sensor on **GPIO34 (ADC1_CH6)**, because ADC2 can't be used while Wi-Fi is on. Add a **100 nF ceramic capacitor** from GPIO34 to GND.
- **Soil sensor:** check the board before use. It needs a **662K regulator**, a **TLC555** (not an NE555) and **R4 (1 MΩ) connected to GND**. The firmware reports a filtered 12-bit raw count (64 samples, trimmed mean) and a percentage computed from **two-point calibration (dry in air, wet in water)**. Calibration is stored in NVS and set from a USB serial command.
- **Config and secrets:** nothing secret goes in git. Wi-Fi and MQTT settings are entered once in a captive portal (WiFiManager) and stored in NVS. For development, an optional gitignored `secrets.ini` supplies build-time defaults.
- **Power:** USB from a 5 V phone charger. **No deep sleep**: the board stays connected so the retained `online`/`offline` status means something, and there's no battery to save.
- **OTA:** LAN-only `ArduinoOTA` with a password, using a partition table with two OTA slots (`min_spiffs.csv`). It's a late, optional task; USB flashing always works.
- **Testing:** all logic goes in a hardware-free library `lib/olvar_core`, tested on the host with `pio test -e native` (Unity). Sensor and connectivity code are thin wrappers, checked by on-device tests (`pio test -e esp32dev`) and a bring-up checklist.
- **Contract:** transport, topics and gateway timestamping are kept. I propose **nullable sensor fields**, an **optional `rssi_dbm`**, and tighter definitions for `soil_raw`/`soil_pct`, `seq`, QoS/retain and `device_id` (see §4).

---

## 2. Research findings (with sources)

### 2.1 Toolchain and framework version
| Finding | Source |
|---|---|
| PlatformIO `espressif32` **8.0.0** was released 2026-10-19. It supports Arduino core **v3.3.12 (IDF v5.5.5)**, ships the ESP-IDF 6.1 framework, and requires PlatformIO Core `^6`. | https://github.com/platformio/platform-espressif32/releases/tag/v8.0.0 , https://github.com/platformio/platform-espressif32/blob/v8.0.0/platform.json |
| PlatformIO officially announced Arduino Core v3 support in the official platform. Before this, core 3.x needed the `pioarduino` fork. | https://piolabs.com/blog/press-releases/esp32-arduino-v3-ai-development.html , https://github.com/sivar2311/platform-espressif32-versions |
| Arduino-ESP32 latest stable is **3.3.12** (2026-09-18). **4.0.0-RC1** (IDF 6.1) is a pre-release only. | https://github.com/espressif/arduino-esp32/releases |
| 8.0.0 broke some projects: unpinned `platform = espressif32` silently upgraded them, and bigger core-3 binaries overflowed the default 1.25 MB app slot. **Pin the exact version** and choose the partition table on purpose. | https://medium.com/@displayengineer/platformio-espressif32-8-0-0-just-broke-a-pile-of-cyd-and-tft-builds-32665a59c7fa |
| The `esp32dev` board definition is 4 MB flash, 240 MHz. | https://github.com/platformio/platform-espressif32/blob/develop/boards/esp32dev.json |
| A tested `platformio.ini` for core v3 on 8.0.0 uses `platform = espressif32 @ ~8.0.0` and `board_build.partitions = min_spiffs.csv`. | https://www.blynk.io/blog/how-to-build-a-blynk-edgent-device-on-esp32-arduino-core-v3-with-platformio |
| Fallback if 8.0.0 misbehaves: pioarduino `55.03.312-1` is the same core 3.3.12. | https://github.com/pioarduino/platform-espressif32/releases/tag/55.03.312-1 |

### 2.2 Pins, ADC and I2C on the ESP32-WROOM-32D
| Finding | Source |
|---|---|
| **ADC2 pins can't be used while Wi-Fi is on.** ADC1 is GPIO32–39. GPIO34–39 are input-only and have no internal pull-up or pull-down. GPIO0/2/5/12/15 are strapping pins. GPIO6–11 and 16–17 are wired to flash/PSRAM. | https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/gpio.html |
| `analogReadMilliVolts()` returns millivolts calibrated with eFuse data. `analogRead()` returns the raw 12-bit value. Measurable range on ESP32 at **11 dB attenuation is 150–3100 mV**. | https://docs.espressif.com/projects/arduino-esp32/en/latest/api/adc.html |
| At 11 dB the readings are accurate up to about 2.45 V. Above that they are non-linear and saturate at 4095 around 3.1 V. | https://solderhub.com/articles/esp32-adc-pins-analog-read-adc1-vs-adc2 |
| Espressif: the ADC is noise-sensitive. Use a **100 nF ceramic bypass capacitor on the ADC pad** plus **multisampling** (their graph uses 64 samples). | https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/adc_calibration.html |
| Vref varies between chips (1000–1200 mV), which is why eFuse calibration exists. | same as above |
| SHT31 address: **0x44** (ADDR low) or **0x45** (ADDR high). BH1750: **0x23** (ADDR low) or **0x5C** (ADDR high). They can share one 3.3 V bus. | https://esp32.co.uk/sht31-breakout-pinout-address-esp32-wiring-arduino , https://esp32.co.uk/bh1750-gy302-pinout-i2c-address-esp32-wiring-arduino |
| Sensirion: don't leave SHT3x ADDR floating. The chip accepts 2.15–5.5 V and I2C up to 1 MHz. | https://gaw.ru/components/sensors-ics/sht31/ (summarises the Sensirion SHT3x-DIS datasheet) |
| Adafruit SHT31-D breakout has 10 kΩ pull-ups on SDA/SCL and a 10 kΩ pull-down on ADR. | https://cdn-learn.adafruit.com/downloads/pdf/adafruit-sht31-d-temperature-and-humidity-sensor-breakout.pdf |
| GY-302 has its own SDA/SCL pull-ups. BH1750 ADDR goes high at ≥0.7×VCC, so tie it to **3V3, never 5 V**. A high-res reading takes 120 ms. | https://www.tinkered.ai/components/bh1750/esp32 |
| 38-pin ESP32 expansion boards have a **jumper that puts 3.3 V or 5 V on every V pin of the G-V-S headers**. It must stay on 3.3 V for ESP32 sensors. | https://protosupplies.com/product/esp32-s-expansion-board-38-pin , https://osoyoo.com/2025/02/07/osoyoo-breakout-board-for-38p-esp32/ |
| Sensirion: heat from nearby electronics raises temperature and lowers RH. At 90 %RH, 1 °C of error is about 5 %RH. **Keep the sensor away from heat sources** such as the MCU and regulators. | https://sensirion.com/media/documents/FC5BED84/662B494D/Sensirion_Humidity_Temperature_Design_Guide.pdf |

### 2.3 Capacitive soil moisture sensor ("v1.2"/"v2.0") hardware flaws
| Finding | Source |
|---|---|
| Three common factory defects. (1) The 662K 3.0 V regulator is missing (pads bridged), so the output follows the supply voltage. (2) An **NE555** is fitted instead of a TLC555; it needs about 4.5 V, so it fails or misbehaves at 3.3 V. (3) **R4 (1 MΩ) is not connected to GND**, so the sensor responds very slowly or returns stale values. | https://github.com/OrangePeachPink/sprout/blob/main/docs/SENSOR_QA.md (cites the Flaura teardown) |
| The missing R4 ground path is confirmed in a peer-reviewed characterisation. Newer batches (May 2020+) have it fixed. | https://pmc.ncbi.nlm.nih.gov/articles/PMC7348898 |
| Fix for (3): solder a 1 MΩ resistor between AOUT and GND. Fix for (2): use ≥4.5 V or swap the chip. The PCB edges are not sealed, so moisture gets in. Coat the edges and the electronics (e.g. PlastiDip). | https://arduinodiy.wordpress.com/2020/08/24/soil-moisture-sensors |
| Test without power. Read the 662K and TLC555 markings, then meter AOUT↔GND, which should be about 1 MΩ. The printed version number doesn't tell you the quality. | https://github.com/OrangePeachPink/sprout/blob/main/docs/user/trust-your-sensor.md |
| The LDO needs headroom (dropout typically 75 mV, up to 350 mV). Some boards combine the LDO with an NE555, and those can't work at all. v2.0 boards can be noisy. | https://miltschek.de/article_2022-06-26_Soil+Moisture+Sensors.html |
| Output is **inverse**: dry gives a high voltage, wet a low one. At 3.3 V, typical readings are about 3.0 V in air and about 1.5 V in water. Calibrate every unit yourself. Insert only up to the marked line. | https://tinkered.ai/components/capacitive-soil-moisture-v12 |
| Calibration: record dry and wet for each board, map raw readings to a percentage, and store the values per device. | https://rustyeddy.com/notes/soil-moisture-sensor |

### 2.4 Libraries
| Finding | Source |
|---|---|
| `RobTillaart/SHT31` 0.5.3 (2026-01-23), MIT, no extra dependencies, SHT30/31/35. | https://github.com/RobTillaart/SHT31 , https://www.arduinolibraries.info/libraries/sht31 |
| Adafruit SHT31 library: last release 2.2.2 in 2023, needs Adafruit BusIO. | https://www.arduinolibraries.info/libraries/adafruit-sht31-library |
| Sensirion official `arduino-i2c-sht3x`, BSD-3, needs Sensirion Core. | https://github.com/Sensirion/arduino-i2c-sht3x |
| `claws/BH1750` 1.3.0. The user code must call `Wire.begin()`. Address is set in `begin()`. | https://github.com/claws/BH1750 , https://github.com/claws/BH1750/blob/master/library.json , https://www.tinkered.ai/components/bh1750/esp32 |
| **PubSubClient is officially unmaintained** (author's notice, 2026). | https://github.com/knolleary/pubsubclient |
| `espMqttClient` 1.7.3. MQTT 3.1.1, all QoS levels, `setWill()`. On ESP32 it runs its own task so `connect()` doesn't block. Builds on Linux for tests. | https://github.com/bertmelis/espMqttClient , https://github.com/bertmelis/espMqttClient/blob/main/docs/index.md , https://registry.platformio.org/libraries/bertmelis/espMqttClient |
| espMqttClient had reconnect-stall bugs in older versions (fixed 1.4.x). Reconnection is the app's job: call `connect()` again after `onDisconnect`. | https://github.com/bertmelis/espMqttClient/issues/102 , https://github.com/bertmelis/espMqttClient/releases |
| ArduinoJson 7.4.3 (2026-03-02). Builds on any C++11 compiler, so it works in host tests. | https://arduinojson.org/v7/revisions/ , https://arduinojson.org/ |
| `tzapu/WiFiManager` 2.0.17. Captive portal, custom parameters, ESP32 support. | https://docs.arduino.cc/libraries/wifimanager , https://solderhub.com/articles/esp32-wifimanager-captive-portal-no-hardcoded-wifi |
| `Preferences` stores key-values in NVS. Namespace and key names are limited to **15 characters**. | https://docs.espressif.com/projects/arduino-esp32/en/latest/tutorials/preferences.html |
| Espressif `WiFiProv` (BLE/SoftAP provisioning) exists but needs the Espressif phone app and can't collect our MQTT fields. | https://github.com/espressif/arduino-esp32/blob/master/libraries/WiFiProv/examples/WiFiProv/WiFiProv.ino , https://randomnerdtutorials.com/esp32-wi-fi-provisioning-ble-arduino |

### 2.5 Wi-Fi, MQTT broker, OTA
| Finding | Source |
|---|---|
| `WiFi.setAutoReconnect()` and `WiFi.onEvent()` (e.g. `ARDUINO_EVENT_WIFI_STA_DISCONNECTED`, which includes a reason code). Event callbacks run in another FreeRTOS task. | https://docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html |
| Layered pattern: auto-reconnect, a disconnect-reason log, and a watchdog that reboots if the link is down longer than N minutes. | https://8bitlabs.ca/Posts/2026/06/25/esp32-wifi |
| **Mosquitto 2.x needs explicit authentication.** Once a listener is configured, `allow_anonymous` defaults to false. Use `mosquitto_passwd` to create users. | https://mosquitto.org/documentation/migrating-to-2-0 , https://mosquitto.org/documentation/authentication-methods |
| Mosquitto 2.1 deprecates `password_file`/`acl_file` in favour of plugins, but both still work until 3.0. | https://mosquitto.org/ChangeLog.txt |
| Arduino network clients can't resolve `*.local` names by themselves. On ESP32, `MDNS.queryHost()` returns an `IPAddress`. | https://github.com/256dpi/arduino-mqtt , https://github.com/espressif/arduino-esp32/issues/10613 |
| OTA needs two app slots plus `otadata`. If an update is interrupted, the old image still boots. PlatformIO uploads OTA with `upload_protocol = espota` and `--auth`. | https://docs.platformio.org/en/latest/platforms/espressif32.html , http://developer.mamezou-tech.com/en/iot/internet-of-things-17 |
| In core 3.x, `ArduinoOTA.setPassword()` stores a SHA-256 hash. MD5 hashes are deprecated. | https://github.com/espressif/arduino-esp32/blob/master/libraries/ArduinoOTA/src/ArduinoOTA.cpp |

### 2.6 Testing
| Finding | Source |
|---|---|
| PlatformIO Test Runner supports **native** (host) and **embedded** (on-board) tests. Native needs a system GCC/Clang on PATH. | https://docs.platformio.org/en/stable/advanced/unit-testing/runner.html |
| Tests live in `test/test_*` folders. `test_filter`/`test_ignore` split them per environment. Code under test should be in `lib/` (`src/` is not built for tests unless `test_build_src = true`). | http://docs.platformio.org/en/latest/advanced/unit-testing/structure/hierarchy.html , https://xa1.at/platformio-unit-test |
| Unity is the default framework and runs both native and embedded. | http://docs.platformio.org/en/latest/advanced/unit-testing/frameworks/unity.html |

### 2.7 Things outside firmware that I checked because the contract mentions them
| Finding | Source |
|---|---|
| The DeepSeek model the user calls "deepseek-flash" **is a real API model id: `deepseek-flash`** (currently DeepSeek-V4.1-Flash-1017; 1M context; JSON Output and tool calls). `deepseek-chat`/`deepseek-reasoner` were retired 2026-07-24. Price per 1M tokens, off-peak: $0.15 input (cache miss), $0.003 (cache hit), $0.60 output. Peak hours cost double. | https://api-docs.deepseek.com/quick_start/pricing , https://api-docs.deepseek.com/updates |

This doesn't affect the firmware. The gateway planner owns this decision; I added it to `.kitchen/notes.md`.

---

## 3. Decisions and alternatives rejected

| Topic | Decision | Rejected alternatives and why |
|---|---|---|
| Build system | **PlatformIO Core 6.x CLI** (VS Code extension optional). `platformio.ini` pins everything. | **Arduino CLI**: no built-in host unit-test runner, and library pinning is weaker. **ESP-IDF directly**: far more code for a beginner project, and the sensor libraries are Arduino-based. **pioarduino fork**: no longer needed since 8.0.0. Kept as the documented fallback. |
| Platform / core | `platform = espressif32 @ 8.0.0` (exact pin), Arduino core 3.3.12 | `~8.0.0`: auto-upgrades patch versions, which is how other projects broke (§2.1). Core **4.0.0-RC1**: pre-release only. |
| Partition table | `board_build.partitions = min_spiffs.csv` (two ~1.9 MB OTA app slots, small SPIFFS) | `default.csv`: 1.25 MB slots may overflow with core 3 + WiFiManager + MQTT. `huge_app.csv`: no second OTA slot. |
| SHT31 driver | `robtillaart/SHT31 @ 0.5.3`: single-shot read, CRC checked, heater off | Adafruit: older, pulls in BusIO. Sensirion official: fine, but adds the Sensirion Core dependency. Second choice if RobTillaart causes trouble. |
| BH1750 driver | `claws/BH1750 @ 1.3.0`, `ONE_TIME_HIGH_RES_MODE` (1 lx resolution, chip powers down between reads) | `hp_BH1750`: equally good, less common in tutorials. Continuous mode: no benefit at a 60 s interval. |
| MQTT client | `bertmelis/espMqttClient @ 1.7.3`, sync `espMqttClient` over `WiFiClient` | **PubSubClient**: unmaintained. **arduino-mqtt (256dpi)**: fine, but blocking `loop()` and 128 B default buffers. **ESP32MQTTClient / esp-mqtt wrapper**: hides reconnect logic and is harder to test on the host. |
| JSON | `bblanchon/ArduinoJson @ 7.4.3` | Hand-written `snprintf`: error-prone with `null` and float formatting. |
| Provisioning | **WiFiManager captive portal** on first boot, or when BOOT (GPIO0) is long-pressed (≥3 s) **while the firmware is running**. It sets an NVS flag and reboots into the portal. Holding GPIO0 low *during reset* would enter the ROM download mode instead (GPIO0 is a strapping pin, §2.2). Custom fields: MQTT host, port, user, password, device_id, interval, OTA password. | **Hard-coded `secrets.h`**: credentials end up in the binary and need a rebuild for every change (kept only as gitignored dev defaults). **Espressif WiFiProv/BLE**: needs the Espressif app and can't collect MQTT fields. **Improv-serial**: needs a browser tool and adds another dependency. |
| Settings storage | `Preferences` namespace `olvar` (keys ≤15 chars). Wi-Fi credentials stay in the Wi-Fi driver's own NVS store, written by WiFiManager. | SPIFFS/LittleFS JSON file: more code and more ways to corrupt. EEPROM emulation: legacy. |
| Broker address | Store a host string. If it ends in `.local`, resolve with `MDNS.queryHost()`, otherwise normal DNS/IP. **Recommend a DHCP reservation for the Pi** and enter its IP. | mDNS only: not reliable on some routers. Fixed IP baked into the firmware: breaks when the network changes. |
| MQTT security | LAN, port 1883, username+password per device (Mosquitto 2.x default requires auth). No TLS in v1. | TLS on 8883: needs certificate distribution and NTP for validity checks. Too much for v1 on a home LAN. Listed as a later option. |
| Timestamps | Firmware sends no wall-clock time. The gateway stamps on receipt, per contract. **The firmware does not buffer readings** while offline. | ESP32 ring buffer + replay: replayed readings would get wrong gateway timestamps. Adding SNTP + device `ts`: possible later; not needed for 60 s plant data where small gaps are fine. |
| Telemetry QoS | Telemetry **QoS 0, not retained**. Status **QoS 1, retained**, with last will `"offline"`. | QoS 1 telemetry: unacked messages are resent after reconnect and arrive late, so the gateway stamp is wrong. On a LAN, TCP already makes loss rare. |
| Soil sampling | Sensor powered from 3V3 all the time. 64 samples ~1 ms apart. Sort, drop the lowest and highest 16, average the middle 32 (trimmed mean), report the 12-bit count. 100 nF on the pin. | Power the sensor from a GPIO: a board with the R4 defect then needs long settling, and there's no battery to save. `analogReadMilliVolts`: also fine, but the raw count matches the contract field name, and calibration removes the per-chip offset anyway. |
| Soil % formula | `pct = clamp(100 × (dry − raw) / (dry − wet), 0, 100)`, rounded to 0.1. `null` if uncalibrated or `dry − wet < 200` counts. | A multi-point/soil-specific curve: overkill for one pot. Users care about "needs water" vs "watered". |
| Calibration UX | USB serial commands (`cal dry`, `cal wet`, `cal show`, `cal reset`). Each averages 10 readings over ~10 s and saves to NVS. Also editable in the portal as two numbers. | MQTT command topic: a contract addition with security questions. Not needed while the user has USB. Dashboard calibration UI: spans three repos. Later option. |
| Power | USB 5 V from a ≥1 A phone charger. **No deep sleep.** Wi-Fi default modem-sleep stays on. | Deep sleep: each wake would drop MQTT, the retained status would flap `online`/`offline`, reconnect takes seconds, and on USB there's nothing to save. Powering from a Pi USB port: shares the Pi's limited USB budget; acceptable as a fallback. |
| OTA | `ArduinoOTA` on the LAN with a password (SHA-256 in core 3.x), late optional task (T7) | HTTP pull OTA from the Pi: needs a release server. Too much for one device. No OTA at all: acceptable. USB always works as the fallback. |
| Watchdogs | Software: if there's no successful MQTT publish for 10 min, `ESP.restart()`. Plus the core's default task watchdog. | Restarting on any Wi-Fi drop: too aggressive. |

---

## 4. Contract changes

Items 1–3 are the parts that touch the firmware. **The transport (Wi-Fi + MQTT to Mosquitto on the Pi), the topic names and gateway-side timestamping are kept as-is.** Items 3–6 (Supabase, avatar, dashboard) have no firmware impact, and I propose no changes to them.

### Changes (gateway must implement)

**C1. Sensor fields are nullable (change).**
`temperature_c`, `humidity_pct`, `lux`, `soil_raw` and `soil_pct` may be JSON `null` when that sensor's read fails (I2C error, CRC failure, sensor missing). The message is still sent so the other sensors' values aren't lost.
*Reason:* breadboard jumper wires and cheap modules fail independently. Dropping the whole message, or sending `0`/`-999`, would hide faults or corrupt charts.
*Impact:* the gateway validator and the `readings` columns must allow NULL. The dashboard shows a gap.

**C2. `soil_pct` is `null` until the soil sensor is calibrated (change).**
*Reason:* an uncalibrated percentage is meaningless.
*Impact:* same as C1.

**C3. Optional field `rssi_dbm` (int), and gateways must ignore unknown fields (addition).**
*Reason:* Wi-Fi signal strength is the first thing to check when data stops arriving. Ignoring unknown fields lets the firmware add diagnostics without breaking the gateway.
*Impact:* the gateway must not reject unknown keys. It may store `rssi_dbm` or drop it; storing it is not required.

### Clarifications (no schema change; please confirm)

- **C4 `device_id`:** default `olvar-` + the last 3 MAC bytes in lowercase hex (e.g. `olvar-a1b2c3`), overridable in the portal. Allowed characters `[a-z0-9-]`, 1–32 long. It is also the MQTT client id and must equal the `<device_id>` topic segment. The gateway should reject a message whose payload `device_id` doesn't match its topic.
- **C5 `soil_raw`:** 12-bit ADC count (0–4095) at 11 dB attenuation, trimmed mean of 64 samples. **Higher means drier.** It is not millivolts.
- **C6 `soil_pct`:** 0–100 on the **air (0 %) to water (100 %)** scale, clamped, 1 decimal place. Real potting soil usually sits well inside that range, roughly 30–80 %. Avatar care thresholds must be set on this scale, or the user can re-calibrate in the pot (§7).
- **C7 Number formats:** `temperature_c` and `humidity_pct` to 2 dp, `lux` to 1 dp, `uptime_ms` a uint32 that wraps after ~49.7 days, `seq` a uint32.
- **C8 `seq`:** starts at 0 on every boot and +1 per telemetry message. A decrease in `seq`, or in `uptime_ms`, means the device rebooted. `(device_id, seq, uptime_ms)` is unique per boot and can be used as a dedupe key.
- **C9 QoS/retain:** telemetry QoS 0, retain false. Status `olvar/v1/<id>/status`: QoS 1, **retained**, payload is the **plain string** `online` or `offline` (not JSON). The last will is `offline` with retain=true, QoS 1. Before a deliberate reboot (OTA or settings change), the firmware publishes `offline` itself.
- **C10 No device-side buffering:** while the broker is down, readings are skipped, not queued. Data has gaps but correct timestamps, and the gateway's own offline buffer (Pi → Supabase) is unaffected.
- **C11 `fw`:** SemVer plus the git short hash, e.g. `"0.1.0+a1b2c3d"`.
- **C12 Broker auth:** one Mosquitto user per device. The username equals `device_id` (recommended, not required). No TLS in v1, LAN only.
- **C13 Interval:** 60 s default, configurable 10–3600 s in the portal. Whatever the interval, the firmware never publishes more than once every 10 s.

Example telemetry payload. This will be committed as `docs/contract/telemetry.example.json` in task T2, with a JSON Schema next to it:
```json
{"device_id":"olvar-a1b2c3","seq":42,"uptime_ms":2520311,"temperature_c":23.41,"humidity_pct":51.07,"lux":312.5,"soil_raw":2210,"soil_pct":46.3,"fw":"0.1.0+a1b2c3d","rssi_dbm":-61}
```

---

## 5. Architecture and folder layout

### 5.1 Runtime design (single Arduino `loop()`, no blocking waits)
```
boot ─► load Settings (NVS) ─► if no Wi-Fi creds OR portal flag set ─► WiFiManager portal ─► save ─► restart
     └► Wi-Fi STA (autoReconnect, event log) ─► MQTT (espMqttClient, LWT) ─► publish status "online" (retained)
loop:  serial CLI poll │ OTA handle │ scheduler tick (every interval_s):
         read SHT31 ─► read BH1750 ─► sample soil ─► build Reading (nullable fields)
         ─► if MQTT connected: publish telemetry (QoS0) else: count skipped
       link supervisor: Wi-Fi/MQTT backoff reconnect (1 s → 60 s, ±20 % jitter); restart if no publish for 10 min
       status LED: slow blink = no Wi-Fi, fast blink = no MQTT, short flash = published
```

### 5.2 Layering
- `lib/olvar_core/`: **pure C++17, no `Arduino.h`**. Holds all logic that can be unit tested on the host: soil calibration maths, ADC filter, telemetry JSON builder, topic builder, settings validation, CLI command parser, backoff/scheduler maths. Depends only on ArduinoJson, which builds on the host.
- `src/`: thin Arduino adapters (drivers, Wi-Fi, MQTT, NVS, OTA, LED) and `main.cpp`. Each adapter converts hardware results into `olvar_core` types.

### 5.3 Folder layout (target)
```
olvar-firmware/
├── platformio.ini              # envs: esp32dev (firmware), native (host tests); extra_configs = secrets.ini
├── secrets.example.ini         # committed template; secrets.ini is gitignored
├── .gitignore                  # .pio/, secrets.ini, .vscode/ (except extensions.json)
├── README.md                   # quick start: install, flash, provision, calibrate
├── .github/workflows/ci.yml    # pio test -e native ; pio run -e esp32dev
├── docs/
│   ├── PLAN.md                 # this file
│   ├── WIRING.md               # wiring table + photos/diagram (from §7)
│   ├── PROVISIONING.md         # first-time setup, portal fields, factory reset
│   ├── CALIBRATION.md          # soil calibration procedure
│   ├── BRINGUP.md              # hardware bring-up checklist
│   └── contract/
│       ├── telemetry.schema.json
│       └── telemetry.example.json
├── include/
│   └── pins.h                  # all GPIO numbers + I2C addresses in one place
├── lib/olvar_core/
│   ├── library.json
│   └── src/
│       ├── soil.h / soil.cpp           # SoilCalibration, rawToPct()
│       ├── adc_filter.h / .cpp         # trimmedMean(samples, n, trim)
│       ├── reading.h                   # struct Reading with std::optional-like fields
│       ├── telemetry.h / .cpp          # buildTelemetryJson(Reading, Meta, char*, size_t)
│       ├── topics.h / .cpp             # telemetryTopic(id), statusTopic(id), isValidDeviceId()
│       ├── settings.h / .cpp           # struct Settings, defaults, validate()
│       ├── cli.h / .cpp                # parseCommand("cal dry") -> Command
│       └── backoff.h / .cpp            # Backoff (exp + jitter), IntervalTimer (millis wrap-safe)
├── src/
│   ├── main.cpp
│   ├── app.h / app.cpp                 # orchestration (state machine above)
│   ├── sensors/
│   │   ├── sht31_sensor.h / .cpp
│   │   ├── bh1750_sensor.h / .cpp
│   │   ├── soil_sensor.h / .cpp        # analogRead loop -> adc_filter
│   │   └── i2c_bus.h / .cpp            # Wire.begin(21,22,100kHz), scan, bus recovery
│   ├── net/
│   │   ├── wifi_link.h / .cpp          # STA, events, RSSI
│   │   ├── mqtt_link.h / .cpp          # espMqttClient, LWT, backoff, publish
│   │   └── provisioning.h / .cpp       # WiFiManager portal + custom params
│   ├── storage/settings_store.h / .cpp # Preferences <-> Settings
│   ├── ota.h / ota.cpp
│   ├── serial_cli.h / serial_cli.cpp   # Serial line reader -> cli.h -> actions
│   └── status_led.h / status_led.cpp
└── test/
    ├── native/                 # host tests, test_filter for env:native
    │   ├── test_soil/ test_adc_filter/ test_telemetry/ test_topics/
    │   └── test_settings/ test_cli/ test_backoff/
    └── embedded/               # on-board tests, test_filter for env:esp32dev
        ├── test_i2c_bus/       # scan finds 0x44 and 0x23
        └── test_sensors/       # values within plausible ranges
```

### 5.4 `platformio.ini` sketch (for T0; versions pinned)
```ini
[platformio]
default_envs = esp32dev
extra_configs = secrets.ini        ; optional, gitignored; missing file is OK

[env]
build_flags = -std=gnu++17 -Wall -DOLVAR_FW_VERSION=\"0.1.0\"
lib_deps = bblanchon/ArduinoJson @ 7.4.3

[env:esp32dev]
platform = espressif32 @ 8.0.0
board = esp32dev
framework = arduino
monitor_speed = 115200
monitor_filters = esp32_exception_decoder
board_build.partitions = min_spiffs.csv
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
T0 must confirm that `extra_configs` tolerates a missing `secrets.ini`. If it doesn't, T0 commits an empty `secrets.ini` stub and documents `git update-index --skip-worktree`, or uses a pre-script. The git short hash is appended to `OLVAR_FW_VERSION` by a small `extra_scripts` pre-script.

---

## 6. Task breakdown

**Ordering:** T0 runs first and alone. After that, T1–T7 own disjoint files and can run in parallel. T8 integrates them last.

**Shared interfaces:** the signatures below are binding so that parallel workers agree. A worker who needs a change appends it to `.kitchen/notes.md`.

```cpp
// lib/olvar_core/src/reading.h  (created by T0, read-only for others)
struct OptF { bool ok=false; float v=0; };   struct OptI { bool ok=false; int32_t v=0; };
struct Reading { OptF temperature_c, humidity_pct, lux, soil_pct; OptI soil_raw; };
struct Meta    { const char* device_id; uint32_t seq; uint32_t uptime_ms; const char* fw; OptI rssi_dbm; };
// soil.h       struct SoilCalibration{int32_t dry=-1, wet=-1; bool valid() const;};  OptF rawToPct(int32_t raw, const SoilCalibration&);
// adc_filter.h int32_t trimmedMean(int32_t* samples, size_t n, size_t trimEachSide); // sorts in place
// telemetry.h  size_t buildTelemetryJson(const Reading&, const Meta&, char* out, size_t cap); // 0 on overflow
// topics.h     bool isValidDeviceId(const char*); size_t telemetryTopic(const char* id, char* out, size_t cap); size_t statusTopic(...)
// settings.h   struct Settings{char device_id[33]; char mqtt_host[64]; uint16_t mqtt_port=1883; char mqtt_user[33]; char mqtt_pass[65];
//                              uint16_t interval_s=60; SoilCalibration soil; char ota_pass[33];};  bool validate(const Settings&, const char** err);
// cli.h        enum class Cmd{CalDry,CalWet,CalShow,CalReset,Status,Portal,Reboot,Help,Unknown}; Cmd parseCommand(const char* line);
// backoff.h    class Backoff{ public: Backoff(uint32_t minMs,uint32_t maxMs); uint32_t next(uint32_t rand01k); void reset(); };
//              class IntervalTimer{ public: explicit IntervalTimer(uint32_t periodMs); bool due(uint32_t nowMs); };
```

### T0 — Scaffold (first, alone)
**Owns:** `platformio.ini`, `secrets.example.ini`, `.gitignore`, `README.md`, `include/pins.h`, `lib/olvar_core/library.json`, `lib/olvar_core/src/reading.h`, `src/main.cpp` (stub that prints the version), `.github/workflows/ci.yml`, `test/native/test_smoke/`, `scripts/version.py`.
**Acceptance:**
- `pio run -e esp32dev` builds.
- `pio test -e native` passes the smoke test.
- CI runs both.
- `secrets.ini` is gitignored and the build passes without it.
- `pins.h` contains SDA=21, SCL=22, SOIL=34, LED=2, BOOT=0, SHT31_ADDR=0x44, BH1750_ADDR=0x23.

**Verify:** run both commands locally and on CI. No hardware needed.

### T1 — Soil maths and ADC filter (olvar_core)
**Owns:** `lib/olvar_core/src/soil.*`, `lib/olvar_core/src/adc_filter.*`, `test/native/test_soil/`, `test/native/test_adc_filter/`.
**Acceptance:**
- `rawToPct`: dry→0, wet→100, midpoint→50; clamps outside the range.
- Returns `ok=false` when uncalibrated, when `dry<=wet` (wrong order), or when `dry−wet<200`.
- Rounds to 0.1.
- `trimmedMean` of `[0,…]` with outliers ignores them; n=64, trim=16 gives the mean of the middle 32.
- Handles n=1 and trim≥n/2 by falling back to the median.

**Verify:** `pio test -e native -f native/test_soil -f native/test_adc_filter`. No hardware.

### T2 — Telemetry JSON, topics, contract fixtures (olvar_core)
**Owns:** `lib/olvar_core/src/telemetry.*`, `lib/olvar_core/src/topics.*`, `test/native/test_telemetry/`, `test/native/test_topics/`, `docs/contract/telemetry.schema.json`, `docs/contract/telemetry.example.json`.
**Acceptance:**
- The output has exactly the contract keys, plus `rssi_dbm` only when `ok`.
- A field with `ok=false` serialises as `null`.
- Decimal places per C7.
- Returns 0 when the buffer is too small (no truncated JSON).
- The example payload validates against the schema; the test parses the produced JSON back with ArduinoJson.
- Topics are `olvar/v1/<id>/telemetry` and `olvar/v1/<id>/status`.
- `isValidDeviceId` accepts `[a-z0-9-]{1,32}` and rejects `/`, `+`, `#`, upper case, empty.

**Verify:** native tests. Optionally `npx ajv-cli validate -s docs/contract/telemetry.schema.json -d docs/contract/telemetry.example.json`. No hardware. Tell the gateway worker about the fixtures in notes.

### T3 — Settings model, CLI parser, backoff/timer (olvar_core)
**Owns:** `lib/olvar_core/src/settings.*`, `lib/olvar_core/src/cli.*`, `lib/olvar_core/src/backoff.*`, `test/native/test_settings/`, `test/native/test_cli/`, `test/native/test_backoff/`.
**Acceptance:**
- `validate` rejects: empty host, port 0, interval outside 10–3600, invalid device_id, soil dry≤wet (when both are set).
- Defaults are valid except for the empty host.
- The parser is case-insensitive, trims whitespace, and maps unknown input to `Unknown`.
- `Backoff` doubles from min to max, applies ±20 % jitter from the injected random value, and `reset()` returns it to min.
- `IntervalTimer` handles `millis()` wraparound (test at 0xFFFFFF00 → 0x00000100).

**Verify:** native tests. No hardware.

### T4 — Sensor drivers and wiring docs (Arduino adapters)
**Owns:** `src/sensors/*`, `test/embedded/test_i2c_bus/`, `test/embedded/test_sensors/`, `docs/WIRING.md`.
**Acceptance:**
- `I2cBus::begin()` starts Wire at 100 kHz on GPIO21/22, and `scan()` returns the found addresses.
- Bus recovery: after 3 consecutive failures it toggles SCL 9× and runs `Wire.end()/begin()`.
- `Sht31Sensor::read()` does a single-shot, high-repeatability, CRC-checked read and returns `OptF`×2. A failure gives `ok=false`, never garbage.
- `Bh1750Sensor::read()` uses one-time high-res mode and gives `ok=false` for a negative result.
- `SoilSensor::sampleRaw()` sets 11 dB attenuation on GPIO34, takes 64 samples ~1 ms apart, then `trimmedMean(…,16)`.
- No sensor call blocks longer than 200 ms.

**Verify on hardware:** `pio test -e esp32dev -f embedded/*` with the board and sensors attached.
- The scan finds 0x44 and 0x23.
- Temperature is 5–40 °C, RH 10–95 %.
- Lux is >0 under room light and ≤5 with the sensor covered by a finger.
- Soil raw differs by >500 counts between air and a glass of water.

**Without hardware:** compile-only check with `pio run -e esp32dev`.

### T5 — Settings storage and provisioning
**Owns:** `src/storage/settings_store.*`, `src/net/provisioning.*`, `docs/PROVISIONING.md`.
**Acceptance:**
- `SettingsStore::load/save/clear` round-trip every `Settings` field through Preferences namespace `olvar`. Keys ≤15 chars: `dev_id, mq_host, mq_port, mq_user, mq_pass, intv_s, soil_dry, soil_wet, ota_pass, portal`. `provisioning.cpp` also owns the non-blocking BOOT (GPIO0) long-press detector.
- Missing keys fall back to defaults, then to `secrets.ini` build flags (`OLVAR_DEFAULT_*`).
- The portal starts if there are no Wi-Fi credentials, or if the NVS `portal` flag was set by a ≥3 s BOOT long-press at runtime or by the `portal` CLI command. Never by reading GPIO0 during reset, because that is the download-mode strap. AP name is `olvar-setup-<mac6>`; the AP password is printed on serial and given in the docs.
- Custom parameters are pre-filled from the current settings and validated with `validate()` before saving. On error the portal shows the message and doesn't save.
- The portal times out after 5 min and reboots into normal mode if old settings exist.

**Verify on hardware:**
- Erase flash (`pio run -t erase`), flash, join the AP from a phone, fill in the form; the board reboots and joins Wi-Fi.
- Power-cycle; the settings persist.
- Long-press BOOT (≥3 s) while the board is running; it reboots into the portal.

**Without hardware:** compile only. The validation logic is already covered by T3.

### T6 — Wi-Fi and MQTT link
**Owns:** `src/net/wifi_link.*`, `src/net/mqtt_link.*`.
**Acceptance:**
- STA mode with `setAutoReconnect(true)`, hostname = device_id, and disconnect reasons logged.
- `MqttLink` sets client id = device_id, credentials, keepalive 30 s, and the will (`status`, `offline`, QoS 1, retain).
- On connect it publishes retained `online`.
- On disconnect it reconnects with `Backoff(1000, 60000)` driven from `loop()`, never from the callback.
- `publishTelemetry()` returns false when not connected; it never queues.
- `.local` hosts are resolved via `MDNS.queryHost`.
- `goOffline()` publishes `offline` and waits ≤500 ms for it to go out before a deliberate restart.
- Supervisor: no successful publish for 10 min → `ESP.restart()`.

**Verify on hardware + Pi:**
- Run `mosquitto_sub -h <pi> -u <user> -P <pw> -v -t 'olvar/v1/#'`. Status is `online` (retained) and a telemetry line appears every interval.
- Unplug the ESP32. Within about 1.5× keepalive, status changes to `offline`.
- Stop Mosquitto for 2 min, then start it again. The device reconnects without a reboot, and `seq` keeps increasing.
- Turn the router off for 2 min. Wi-Fi reconnects.

**Without hardware:** compile only. Optional host test: espMqttClient builds on Linux, so a worker *may* add a native test against a local `mosquitto` (`brew install mosquitto`). That's optional and not in CI.

### T7 — OTA, serial CLI, status LED, calibration docs
**Owns:** `src/ota.*`, `src/serial_cli.*`, `src/status_led.*`, `docs/CALIBRATION.md`.
**Acceptance:**
- `ArduinoOTA` starts only if `ota_pass` is set. Hostname = device_id. `onStart` calls `MqttLink::goOffline()` through a callback hook.
- The serial CLI reads lines at 115200 and dispatches the `cli.h` commands. `cal dry`/`cal wet` average 10 soil samples over ~10 s, print the result and save it via `SettingsStore`. `cal show` prints dry, wet, the current raw value and the percentage. `status` prints IP, RSSI, MQTT state, seq and free heap.
- LED patterns as in §5.1, all non-blocking.
- `docs/CALIBRATION.md` gives the procedure in §7.4 with screenshots of expected serial output.

**Verify on hardware:**
- Run the calibration on the real sensor. `soil_pct` reads ≈0 in air and ≈100 in water.
- Run `pio run -t upload --upload-port olvar-xxxxxx.local` with `upload_protocol=espota` and `--auth`. The board updates and comes back `online`.

**Without hardware:** compile only.

### T8 — Integration, bring-up, release 0.1.0 (last)
**Owns:** `src/main.cpp`, `src/app.*`, `docs/BRINGUP.md`, version bump.
**Acceptance:**
- Implements the §5.1 state machine using T1–T7.
- The first telemetry is sent ≤5 s after the MQTT connection, then every `interval_s`.
- One sensor unplugged → that field is `null` and the others are still sent.
- Free heap stays steady over a 24 h soak (no downward trend >5 %).
- The full native test suite passes and CI is green.
- `BRINGUP.md` is a step-by-step checklist (§7) that the user can follow.

**Verify:**
- 24 h soak with the Pi, logging `mosquitto_sub` to a file. Expect ≥99 % of the expected messages and no reboot except injected ones (`uptime_ms` grows monotonically).
- Unplug the SHT31 VIN mid-run and check for `null` temperature and humidity, with recovery after re-plugging.
- Cross-repo: the gateway worker inserts these messages into Supabase.

### What can be tested without hardware
- All of `olvar_core` (T1–T3): native Unity tests in CI.
- A compile check of every Arduino adapter (`pio run -e esp32dev`), also in CI.
- Contract fixtures (T2), which the gateway can reuse in its own tests.
- The **gateway** can be developed without the ESP32 by publishing the example payload with `mosquitto_pub` in a loop.

These need the real board: I2C/ADC behaviour, the portal, Wi-Fi/MQTT reconnect, OTA, calibration and the soak test.

---

## 7. Things the user must do by hand

### 7.1 Accounts and installs (your Mac)
1. Install PlatformIO Core. Either `brew install platformio`, or `pipx install platformio`, or the VS Code "PlatformIO IDE" extension. Native tests also need the Xcode command-line tools (`xcode-select --install`) for a host compiler. Install guide: https://docs.platformio.org/en/latest/core/installation/index.html
2. Connect the ESP32 with a **data** USB cable. If no `/dev/cu.*` port appears, install the USB-UART driver for the chip on your board (it's printed near the USB socket: CP2102 or CH340).
3. No cloud accounts are needed for the firmware.

### 7.2 Raspberry Pi (shared with the gateway plan; the firmware needs these)
1. In your router, give the Pi a **DHCP reservation** (fixed IP). Write the IP down.
2. Install Mosquitto (the gateway plan gives the exact steps). Create one user per device: `sudo mosquitto_passwd /etc/mosquitto/passwd olvar-a1b2c3`. Use the device id that the ESP32 prints on its first boot.
3. Confirm the Pi and the ESP32 are on the **same 2.4 GHz Wi-Fi network**. The ESP32 has no 5 GHz radio (https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf).

### 7.3 Check the soil sensor before wiring (5 minutes, sensor unplugged)
1. Read the small 3-pin chip near the connector: it should say **662K** (regulator).
2. Read the 8-pin chip: it must say **TLC555** (or "TL555"/"555C"). **NE555 won't work at 3.3 V**: replace the sensor, or ask for help.
3. With a multimeter on resistance, measure **AOUT ↔ GND**. About **1 MΩ** is good. "OL"/open means R4 isn't grounded: solder a 1 MΩ resistor between AOUT and GND.
4. Coat the PCB edges along the sensing blade, and the electronics at the top, with nail varnish, PlastiDip or heat-shrink. **Insert only up to the white line.**

### 7.4 Wiring table (ESP32 on the 38-pin GPIO expansion board + breadboard)

**First:** set the expansion board's **V-rail jumper to 3.3 V**. Use 3V3/GND from the board, and make two breadboard rows for SDA and SCL so both I2C sensors share them.

| Wire | From (sensor pin) | To (ESP32 / expansion board) | Notes |
|---|---|---|---|
| 1 | SHT31 **VIN** (or VCC) | **3V3** | never 5 V |
| 2 | SHT31 **GND** | **GND** | |
| 3 | SHT31 **SDA** | **GPIO21** (SDA row on breadboard) | shared bus |
| 4 | SHT31 **SCL** | **GPIO22** (SCL row on breadboard) | shared bus |
| 5 | SHT31 **ADR/ADDR** (if present) | **GND** or leave unconnected if the board has a pull-down | → address **0x44** |
| 6 | GY-302 **VCC** | **3V3** | |
| 7 | GY-302 **GND** | **GND** | |
| 8 | GY-302 **SDA** | SDA row (→ GPIO21) | |
| 9 | GY-302 **SCL** | SCL row (→ GPIO22) | |
| 10 | GY-302 **ADDR** | **GND** or leave unconnected | → address **0x23**; never tie to 5 V |
| 11 | Soil sensor **VCC** (red) | **3V3** | 3V3 is safe for every board variant |
| 12 | Soil sensor **GND** (black) | **GND** | |
| 13 | Soil sensor **AOUT** (yellow) | **GPIO34** | ADC1_CH6, input-only. **Not** an ADC2 pin. |
| 14 | 100 nF ceramic capacitor ("104") | between **GPIO34** and **GND**, close to the ESP32 | ADC noise (Espressif) |
| 15 | USB cable | ESP32 board's own USB port → 5 V ≥1 A charger | the same port is used for flashing |

**Pins not to use for sensors:** GPIO0/2/5/12/15 (boot strapping), GPIO6–11 and 16–17 (flash), and all ADC2 pins (0, 2, 4, 12–15, 25–27) for analog.

**Placement:** put the SHT31 on 15–20 cm wires **away from the ESP32 board and the charger**, out of direct sun and not above the soil surface after watering. Point the GY-302 light window up, at plant level, uncovered. Keep I2C wires ≤30 cm.

### 7.5 First boot and provisioning
1. `pio run -t upload && pio device monitor`. The serial monitor prints the device id and the setup AP password.
2. On your phone, join Wi-Fi **`olvar-setup-xxxxxx`**. The portal opens (if not, browse to 192.168.4.1).
3. Choose your 2.4 GHz network and enter its password. Then enter the MQTT host (the Pi's IP), port 1883, the MQTT user and password from §7.2, the interval (60), and optionally an OTA password. Save.
4. On the Pi, `mosquitto_sub -h localhost -u <admin> -P <pw> -v -t 'olvar/v1/#'` should show `online` and then telemetry every 60 s.
5. To change settings later: while the board is running normally, press and hold **BOOT** for 3 s, then release. It reboots into the portal. Don't hold BOOT while pressing EN/RST: that puts the chip into flashing mode, and you'll need to press EN again.

### 7.6 Soil calibration (needs the USB serial monitor)
1. Wipe the sensor dry and hold it in the air. Type `cal dry`, wait about 10 s, and note the value (typically about 2600–3300).
2. Put it in a glass of water **up to the line only**. Type `cal wet`, wait about 10 s (typically about 1200–1800).
3. Type `cal show`: it should give ≈0 % in air and ≈100 % in water. Then put the sensor into the pot, 2/3 of the way to the root zone, at the same depth from then on.
4. Optional, gives a more meaningful scale for the plant: repeat `cal dry` in the pot when the top 2–3 cm of soil is dry and the peace lily is about to need water, and `cal wet` 30 minutes after a thorough watering. Tell the gateway/dashboard which scale you used (§4 C6).

---

## 8. Risks and open questions for the user

### Open questions (answers change the plan)
1. **Which exact ESP32 board and expansion board?** I assumed a 38-pin DevKitC-style board (`esp32dev`, 4 MB flash) and a G-V-S expansion board with a 3.3/5 V jumper. A photo of both, top side, would settle it. A 30-pin board uses the same GPIO numbers, but the header positions differ.
2. **What Wi-Fi network will it live on?** It must be **2.4 GHz WPA2/WPA3-Personal**. A school or company network (WPA2-Enterprise/eduroam, or a captive login page) won't work with the portal flow. Then you'd need a home router or a dedicated hotspot.
3. **Do you want OTA?** It's planned as optional (T7). If the plant sits within reach of your laptop's USB cable, we can drop it.
4. **Soil % scale:** keep the simple air/water scale (default), or do the in-pot calibration so that 0 % means "water now"? The gateway's peace-lily thresholds depend on this.
5. **Sensor check results (§7.3):** does your soil sensor have the 662K, a TLC555 and ~1 MΩ? If it has an NE555, order a replacement before T4.
6. **Is 60 s the right interval?** For a houseplant, 5 min would also be fine and makes less data. 60 s is the contract default and the firmware supports 10–3600 s.

### Risks
| Risk | Likelihood | Mitigation |
|---|---|---|
| Faulty cheap soil sensor (NE555 / floating R4 / missing regulator) | High (defects are reported on many boards) | §7.3 check, 1 MΩ fix, replace if NE555 |
| Soil sensor drifts over months as moisture gets into its edges | Medium | Coat the edges. Re-run `cal` every few months. `soil_raw` is always stored, so the percentage can be recomputed. |
| Dry-air reading sits near the ADC's non-linear top (>2.45 V at 11 dB) | Medium | Calibration absorbs it. Readings only clip at 4095 (~3.1 V); if dry ever reads 4095, use in-pot calibration instead. |
| Temperature reads high because the ESP32 or charger warms the SHT31 | Medium | Place the SHT31 15–20 cm away. Optionally add a fixed offset later. |
| I2C errors from long or loose breadboard jumper wires | Medium | 100 kHz, short wires, the module pull-ups are enough, bus recovery and `null` fields (C1). |
| PlatformIO 8.0.0 is only 4 weeks old; regressions are possible | Low–Medium | Exact pin. Fallback to pioarduino `55.03.312-1` (same core 3.3.12). |
| Firmware too big for an OTA slot | Low | `min_spiffs.csv` (~1.9 MB per slot). CI prints the size. |
| No TLS on MQTT: LAN sniffing could reveal the MQTT password | Low (home LAN) | Per-device credentials. TLS on 8883 is a later option. |
| USB power from the Pi browns out the ESP32 during Wi-Fi bursts | Low | Use a separate ≥1 A charger. |
| `mosquitto` password_file is deprecated in 2.1 | Low | Still supported until 3.0. The gateway plan may use the password-file plugin; no firmware impact. |
| espMqttClient reconnect edge cases | Low | The app-level backoff loop plus the 10-minute no-publish restart supervisor. |
