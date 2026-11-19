# olvar-firmware — Design

Scope: the ESP32 firmware that reads the plant sensors and publishes them over MQTT to the Raspberry Pi 3B gateway.
Research date: 2026-11-16. Every non-obvious claim links to its source.
**This design follows the final cross-repo contract (§4).** That contract overrides the provisional one and any earlier choice in this file.

---

## 1. Summary

- **Toolchain:** PlatformIO Core 6.x with the **official `platformio/espressif32` platform pinned to `8.0.0`**. That release (2026-10-19) ships **Arduino-ESP32 core 3.3.12 on ESP-IDF 5.5.5**, so the community `pioarduino` fork is no longer needed. Board `esp32dev`, framework `arduino`, partition table `huge_app.csv` (firmware updates are over USB only, so there's no OTA slot).
- **Hardware (confirmed):** a 38-pin ESP32-WROOM-32D dev board on an expansion board with a 3.3 V/5 V jumper, plus a breadboard. Normal 2.4 GHz home Wi-Fi with no login page.
- **Libraries:** `RobTillaart/SHT31` (temperature/humidity), `claws/BH1750` (light), `bertmelis/espMqttClient` (MQTT 3.1.1, last will), `bblanchon/ArduinoJson` 7 (payload), `tzapu/WiFiManager` (first-time Wi-Fi + MQTT settings portal). `Preferences` (NVS) comes with the core.
- **Pins:** I2C bus on **GPIO21 (SDA) / GPIO22 (SCL)**, shared by SHT31 @ 0x44 (0x45 also accepted) and BH1750 @ 0x23, all at 3.3 V. **DHT11 fallback on GPIO4** (single-wire, 10 kΩ pull-up to 3V3), used only when no SHT31 answers at boot. Capacitive soil sensor on **GPIO34 (ADC1_CH6)**, because ADC2 can't be used while Wi-Fi is on. A **100 nF ceramic capacitor** from GPIO34 to GND is recommended (the 64-sample filter copes without it).
- **Soil sensor:** a generic capacitive soil moisture sensor, 3.3–5.5 V (98×23 mm board). Its manual says built-in regulator, 0–3.0 V output; it doesn't name a version (§2.3). Cheap boards often ship with defects, so instead of a chip/multimeter inspection the design uses:
  - a **safe default wiring**: VCC on **3V3**, never 5 V;
  - a **serial sanity check** (`soilcheck`, §7.3) that compares the raw reading in air and in a glass of water.
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
| Another vendor's datasheet for the same design gives about 2.5 V in dry air and about 1.0 V submerged, and ~5 mA supply current. Typical values vary by about ±0.5 V between boards. | https://shillehtek.com/blogs/shillehtek-product-manuals/capacitive-soil-moisture-sensor-v1-2-manual |
| An unconnected ESP32 ADC pin reads random values. | https://docs.espressif.com/projects/esp-idf/en/release-v4.4/esp32/api-reference/peripherals/adc.html |
| **The sensor used here:** a generic capacitive soil moisture sensor, 3.3–5.5 V. Its listing doesn't name v1.2 or v2.0. The seller's manual says: "built-in voltage regulator chip", **3.3–5.5 V DC**, **output 0–3.0 V DC**, PH2.0-3P connector, 97×23×7 mm, 12 g, cable included. It doesn't say which timer chip is fitted. | seller's listing and manual |
| **Conclusion for this model:** it's the common 98×23 mm DFRobot-SEN0193-style board, sold as "v1.2" or "v2.0" depending on the batch. The seller claims a regulator, but the defects above are batch-dependent and the listing can't rule them out. So: power at 3.3 V, which is safe even if the regulator is missing, and confirm it works with the software check in §7.3. | (inference from the two rows above and §2.3 sources) |

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
| DHT11 fallback | Probe SHT31 0x44, then 0x45; if neither ACKs, one DHT11 read on GPIO4; keep the first that answers until reboot (none: retry on every read). Own ~60-line driver: 20 ms start pulse via `delay()`, ≤ 5 ms bit timing in a core-1 critical section, checksum checked, ≥ 2 s after power-up and from the end of one transaction to the next start (a call inside the window returns that attempt's result, so a failure stays `null`). The DHT11 replies with the sample taken after the previous read, so a due telemetry message first triggers a conversion and is sent ~2 s later with the fresh value; the loop keeps running meanwhile. The first two replies after power-up can be unsettled (5 %RH seen on real hardware), so a message whose reply would be one of them sends `null`. Added because the SHT31 burnt out during bring-up and a bare ASAIR DHT11 was the spare. GPIO4: not a strapping pin, not flash, free on the 38-pin board. | Adafruit DHT library: pulls in Adafruit Unified Sensor for two numbers. RMT-based reader: more code for a read once a minute. Runtime switching between sensors: not needed. |
| BH1750 driver | `claws/BH1750 @ 1.3.0`, `ONE_TIME_HIGH_RES_MODE` | `hp_BH1750`: fine, less common. Continuous mode: no benefit at 60 s. |
| MQTT client | `bertmelis/espMqttClient @ 1.7.3`, sync client over `WiFiClient` | **PubSubClient**: unmaintained. **arduino-mqtt**: blocking `loop()`, small buffers. **ESP32MQTTClient**: hides reconnect logic. |
| JSON | `bblanchon/ArduinoJson @ 7.4.3` | Hand-written `snprintf`: error-prone with `null`. |
| Provisioning | **WiFiManager captive portal** on first boot, or when BOOT (GPIO0) is long-pressed (≥3 s) **while the firmware is running**. The long-press sets an NVS flag and reboots into the portal. Holding GPIO0 low *during reset* would enter ROM download mode instead, because GPIO0 is a strapping pin. Portal fields: device_id, MQTT host, port, password. **The MQTT username is always the device_id**, because the gateway's broker ACL requires username == client id == device_id. | Hard-coded `secrets.h`: credentials end up in the binary and need a rebuild for every change (kept only as gitignored dev defaults). Espressif WiFiProv/BLE: needs the Espressif app and can't collect MQTT fields. |
| Settings storage | `Preferences` namespace `olvar`. Wi-Fi credentials stay in the Wi-Fi driver's own NVS store, written by WiFiManager. | LittleFS JSON: more code. EEPROM emulation: legacy. |
| Broker address | Host string; `.local` names resolve via `MDNS.queryHost()`. **DHCP reservation for the Pi recommended.** | mDNS only: unreliable on some routers. |
| MQTT security | LAN, port 1883, username+password per device, **no TLS** (decision F) | TLS: out of v1. |
| Timestamps / buffering | No wall-clock time on the device; the gateway stamps on receipt. **Readings are dropped while the broker is unreachable** (decision C). | ESP32 ring buffer + replay: replayed readings would get wrong gateway stamps. |
| Telemetry QoS | **QoS 1, not retained** (decision C). On disconnect the firmware discards any unacknowledged telemetry rather than resending it later, so a late resend can't get a stale gateway timestamp. T6 must verify espMqttClient's outbox behaviour and clear it on disconnect if needed. While still connected, the client may legitimately retransmit a QoS 1 message whose PUBACK is late (default 10 s timeout), so the broker can deliver the same `(boot_id, seq)` twice. That's normal QoS 1 at-least-once delivery; the gateway's dedup keeps one stored reading. | QoS 0: rejected by the final contract. |
| `boot_id` | 8 lowercase hex chars from `esp_random()`, generated once per boot after Wi-Fi starts (hardware RNG) | MAC- or counter-based ids: not random; an NVS boot counter wears flash for no benefit. |
| Soil sampling | Sensor powered from 3V3 all the time. 64 samples ~1 ms apart: sort, drop the lowest and highest 16, average the middle 32. Report the 12-bit count. 100 nF on the pin. | Power from a GPIO: a board with the R4 defect then needs long settling. `analogReadMilliVolts`: the contract field is a raw count. |
| Soil calibration | **None in firmware** (decision D). Dry/wet values live in the database and are set from the dashboard. | Serial `cal` commands / NVS calibration (earlier draft): removed. |
| Soil sensor pre-check | **No chip reading, no multimeter.** VCC on 3V3. The serial `soilcheck` command streams `soil_raw` and judges the air/water span (§7.3). It's diagnostic only: nothing is stored or sent, so it doesn't conflict with decision D. | Visual chip check + multimeter: the chip markings are hard to identify without experience. Feeding 5 V: if the regulator is missing on the delivered board, AOUT could exceed 3.3 V and damage GPIO34. |
| Interval | Fixed **60 s** compile-time constant (`OLVAR_INTERVAL_S`), not a portal field (decision E) | Configurable interval: not needed for v1. |
| Power | USB 5 V from a ≥1 A charger. **No deep sleep.** Default Wi-Fi modem-sleep. | Deep sleep: the MQTT connection and retained status would flap on every wake, for no gain on USB. |
| Watchdogs | No successful publish for 10 min → `ESP.restart()`, plus the core's task watchdog | Restart on any Wi-Fi drop: too aggressive. |

---

## 4. Contract changes

The **final contract** replaces the provisional one. Below is the firmware-facing part as the firmware implements it, then what changed relative to the provisional contract and the first draft. Nothing is still open on the firmware side.

### 4.1 Final firmware contract
- **Transport:** Wi-Fi to the Mosquitto broker on the Pi 3B, port 1883, unencrypted on the LAN, per-device password. **MQTT username = client id = `device_id`** (gateway ACL: a device may write only `olvar/v1/<username>/…`).
- **Telemetry topic** `olvar/v1/<device_id>/telemetry`, **QoS 1**, not retained, every **60 s**.
  ```json
  {"device_id":"olvar-a1b2c3","boot_id":"9f3c07ab","seq":42,"uptime_ms":2520311,
   "temperature_c":23.41,"humidity_pct":51.07,"lux":312.5,"soil_raw":2210,
   "rssi_dbm":-61,"fw":"0.1.0+a1b2c3d"}
  ```
  | Field | Type | Firmware rule |
  |---|---|---|
  | `device_id` | string | default `olvar-` + last 3 MAC bytes as 6 lowercase hex; overridable in the portal. **Must match the gateway rule `^[a-z0-9][a-z0-9-]{2,31}$`** (3–32 chars, first char a letter or digit), and **`olvar-gateway` is reserved**. Equals the topic segment, the MQTT client id and the MQTT username. |
  | `boot_id` | string | 8 lowercase hex chars, random per boot |
  | `seq` | uint32 | 0 at boot, +1 for each message handed to the MQTT client. Strictly increasing and **never reused for a different reading**. A subscriber may see the *same* message (identical payload, same `seq`) more than once because of QoS 1 retransmission; the gateway dedups on `(device_id, boot_id, seq)`. Gaps are possible when an in-flight QoS 1 message is discarded on disconnect. Intervals skipped while already disconnected don't use a number. |
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
| Item | Provisional contract / first draft | Final |
|---|---|---|
| Telemetry QoS | first draft: QoS 0 | **QoS 1**, unacked telemetry discarded on disconnect |
| `boot_id` | absent | **added**, 8 hex, random per boot |
| `soil_pct` | sent by firmware, calibration in NVS via serial `cal` | **removed from firmware**; calibration in the DB via the dashboard; `soil_pct` computed server-side |
| Nullable fields, no NaN | firmware proposal | **adopted** |
| `rssi_dbm` optional, unknown fields ignored | firmware proposal | **adopted** |
| Dedup key | first draft: `(device_id, seq, uptime_ms)` | `(device_id, boot_id, seq)` |
| Interval | configurable 10–3600 s | **fixed 60 s** |
| OTA | optional ArduinoOTA | **removed**; USB only |
| `device_id` rule | first draft: `[a-z0-9-]{1,32}` | gateway rule `^[a-z0-9][a-z0-9-]{2,31}$`, `olvar-gateway` reserved; MQTT username = device_id |

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
├── tools/sim_telemetry.sh      # MQTT telemetry simulator (incrementing seq, per-boot boot_id)
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
│       ├── soil_check.h / .cpp         # judgeSoilCheck() verdicts for the serial soilcheck
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
    ├── native/   test_adc_filter/ test_sanitize/ test_soil_check/ test_telemetry/ test_ids/ test_settings/ test_cli/ test_backoff/
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

**Ordering:** T0 comes first. After that, T1–T7 own disjoint files and are independent of each other. T8 integrates them last.

**Shared interfaces:** the signatures below are the contract between the modules.

```cpp
// lib/olvar_core/src/reading.h  (created by T0, read-only for others)
struct OptF { bool ok=false; float v=0; };   struct OptI { bool ok=false; int32_t v=0; };
struct Reading { OptF temperature_c, humidity_pct, lux; OptI soil_raw; };
struct Meta    { const char* device_id; const char* boot_id; uint32_t seq; uint32_t uptime_ms; const char* fw; OptI rssi_dbm; };
// adc_filter.h int32_t trimmedMean(int32_t* samples, size_t n, size_t trimEachSide);   // sorts in place
// soil_check.h enum class SoilVerdict{Ok,Saturated,NoSignal,Noisy,Swapped,Stuck,Slow,SmallSpan};
//              struct SoilWindow{int32_t min,max,last; uint32_t settleMs;};   // stats over one air or water phase
//              SoilVerdict judgeSoilCheck(const SoilWindow& air, const SoilWindow& water);  // diagnostic only, never stored/sent
// sanitize.h   OptF finiteOrNull(float v); float roundTo(float v, int decimals);
// telemetry.h  size_t buildTelemetryJson(const Reading&, const Meta&, char* out, size_t cap); // 0 on overflow
// ids.h        bool isValidDeviceId(const char*);   // ^[a-z0-9][a-z0-9-]{2,31}$ and != "olvar-gateway"
//              void deviceIdFromMac(const uint8_t mac[6], char out[13]);
//              void bootIdFromRandom(uint32_t r, char out[9]);
//              size_t telemetryTopic(const char* id, char* out, size_t cap); size_t statusTopic(const char* id, char* out, size_t cap);
// settings.h   struct Settings{char device_id[33]; char mqtt_host[64]; uint16_t mqtt_port=1883; char mqtt_pass[65];}; // MQTT username = device_id
//              bool validate(const Settings&, const char** err);
// cli.h        enum class Cmd{Status,Raw,SoilCheck,Portal,Reboot,Help,Unknown}; Cmd parseCommand(const char* line);
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
- `pins.h` defines SDA=21, SCL=22, SOIL=34, LED=2, BOOT=0, DHT11=4, SHT31_ADDR=0x44 (alt 0x45), BH1750_ADDR=0x23, `OLVAR_INTERVAL_S`=60.

**Verify:** run both commands locally and on CI. No hardware needed.

### T1 — ADC filter, sanitiser, soil-check verdict (olvar_core)
**Owns:** `lib/olvar_core/src/adc_filter.*`, `lib/olvar_core/src/sanitize.*`, `lib/olvar_core/src/soil_check.*`, `test/native/test_adc_filter/`, `test/native/test_sanitize/`, `test/native/test_soil_check/`.
**Acceptance:**
- `trimmedMean` with n=64, trim=16 gives the mean of the middle 32 and ignores outliers. n=1 and trim≥n/2 fall back to the median.
- `finiteOrNull` returns `ok=false` for NaN and ±Inf.
- `roundTo` rounds half away from zero.
- `judgeSoilCheck` implements the §7.3 table exactly, with the thresholds as named constants. Every verdict has a test case with a boundary value on each side.

**Verify:** `pio test -e native -f native/test_adc_filter -f native/test_sanitize -f native/test_soil_check`. No hardware.

### T2 — Telemetry JSON, ids/topics, contract fixtures (olvar_core)
**Owns:** `lib/olvar_core/src/telemetry.*`, `lib/olvar_core/src/ids.*`, `test/native/test_telemetry/`, `test/native/test_ids/`, `docs/contract/telemetry.schema.json`, `docs/contract/telemetry.example.json`, `tools/sim_telemetry.sh`.
**Acceptance:**
- The output has exactly the §4.1 keys, plus `rssi_dbm` only when `ok`. **No `soil_pct` key.**
- A field with `ok=false`, NaN or Inf serialises as `null`.
- Decimal places per §4.1.
- Returns 0 when the buffer is too small (no truncated JSON).
- The test parses the produced JSON back with ArduinoJson and checks it against the example.
- Topics are `olvar/v1/<id>/telemetry` and `olvar/v1/<id>/status`.
- `isValidDeviceId` implements the gateway rule `^[a-z0-9][a-z0-9-]{2,31}$` and rejects `olvar-gateway`.
  - Accepts: `abc`, `olvar-a1b2c3`, 32-char ids.
  - Rejects: `a`, `ab`, `-plant`, `olvar-gateway`, 33-char ids, `/`, `+`, `#`, upper case, empty.
  - The schema's `device_id` pattern is the same regex.
- `deviceIdFromMac` gives `olvar-` + the last 3 bytes in lowercase hex, and its output always passes `isValidDeviceId`.
- `bootIdFromRandom` gives exactly 8 lowercase hex chars, zero-padded.
- `tools/sim_telemetry.sh` is a simulator for developing without the board:
  - Usage: `HOST=… DEVICE=… PASS=… [INTERVAL=60] [REBOOT_EVERY=0] tools/sim_telemetry.sh`.
  - It picks a random `boot_id` once (`openssl rand -hex 4`) and starts `seq` at 0, with `uptime_ms` growing.
  - Each loop publishes one payload with `mosquitto_pub -q 1` (as the device user), then increments `seq`. Sensor values are random-walked within plausible ranges; with `NULL_EVERY=n`, every nth message has `temperature_c` set to `null`.
  - It publishes retained `online` on start and `offline` on Ctrl-C.
  - If `REBOOT_EVERY=n`, every n messages it picks a new `boot_id` and resets `seq` and `uptime_ms` to 0, to simulate a reboot.

**Verify:**
- Native tests.
- Run `shellcheck tools/sim_telemetry.sh`.
- Run the simulator against a local broker with `mosquitto_sub -v` and check that `seq` increments and `boot_id` is stable until a simulated reboot.
- Optionally `npx ajv-cli validate -s docs/contract/telemetry.schema.json -d docs/contract/telemetry.example.json`.

No hardware. The gateway reuses the fixtures and the simulator.

### T3 — Settings model, CLI parser, backoff/timer (olvar_core)
**Owns:** `lib/olvar_core/src/settings.*`, `lib/olvar_core/src/cli.*`, `lib/olvar_core/src/backoff.*`, `test/native/test_settings/`, `test/native/test_cli/`, `test/native/test_backoff/`.
**Acceptance:**
- `validate` rejects: empty host, port 0, empty password, and any device_id that fails `isValidDeviceId` (gateway rule + reserved `olvar-gateway`). The error message names the rule so the portal can show it.
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
- `ClimateSensor` uses an SHT31 at 0x44/0x45 if one answers at boot, else a DHT11 on GPIO4 (checksum checked, ≤ 1 read per 2 s, failure gives `ok=false`); with neither it keeps probing and returns `ok=false`.
- `Bh1750Sensor::read()` uses one-time high-res mode; a negative result gives `ok=false`.
- `SoilSensor::sampleRaw()` sets 11 dB attenuation on GPIO34, takes 64 samples ~1 ms apart, then `trimmedMean(…,16)`.
- No call blocks longer than 200 ms.
- `WIRING.md` contains §7.3–§7.4.

**Verify on hardware:** `pio test -e esp32dev -f embedded/*`.
- The scan finds 0x23, and 0x44 or 0x45 when an SHT31 is fitted.
- SHT31: temperature 5–40 °C, RH 10–95 %. DHT11: 0–50 °C, 20–90 %RH.
- Lux is >0 under room light and ≤5 when covered.
- `soil_raw` differs by >500 counts between air and a glass of water.

**Without hardware:** compile only (`pio run -e esp32dev`).

### T5 — Settings storage and provisioning
**Owns:** `src/storage/settings_store.*`, `src/net/provisioning.*`, `docs/PROVISIONING.md`.
**Acceptance:**
- `SettingsStore` load/save/clear round-trips `Settings` in Preferences namespace `olvar`. Keys: `dev_id, mq_host, mq_port, mq_pass, portal`. There's no username key: the MQTT username is always `device_id`.
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
- `MqttLink` sets client id = username = device_id, the password, clean session, keepalive 30 s, and the will (`status`, `offline`, QoS 1, retain). On connect it publishes retained `online`.
- `MqttLink` keeps a `skipped` counter for intervals dropped while disconnected. T7's `status` command prints it.
- `seq` is incremented only when `publishTelemetry()` hands a message to the client, so skipped intervals don't use a number.
- A message discarded unacknowledged on disconnect keeps its number: `seq` is never reused, because the broker may already have received it. That leaves a gap of 1 or more.
- `publishTelemetry()` publishes at QoS 1 and returns false when not connected; it never queues while disconnected.
- **On disconnect, any unacked telemetry is discarded.** Check whether espMqttClient keeps its outbox across reconnects; if it does, clear it. Document the result in the code.
- Reconnect uses `Backoff(1000, 60000)` from `loop()`, never from a callback.
- `.local` hosts are resolved via `MDNS.queryHost`.
- `goOffline()` publishes `offline` (≤500 ms) before a deliberate restart.
- Supervisor: no successful publish for 10 min → restart.

**Verify on hardware + Pi:**
- `mosquitto_sub -h <pi> -u <user> -P <pw> -v -q 1 -t 'olvar/v1/#'` shows `online` (retained) and telemetry every 60 s with `boot_id` constant and `seq` increasing.
- Unplug the ESP32. Within about 1.5× keepalive, status changes to `offline`.
- Stop Mosquitto for 2 min, then start it again. Check that:
  - the device reconnects without a reboot (`boot_id` unchanged);
  - the **distinct** `seq` values keep increasing; no `seq` ever carries two different payloads. Exact duplicates of one message (same `seq`, identical payload) from QoS 1 retransmission are allowed. Gaps are allowed only for messages discarded in flight (e.g. 42 → 44 when 43 was unacknowledged at the moment the broker stopped). The gap is at most the number of in-flight messages, normally ≤1, so it's always far smaller than the number of skipped intervals;
  - dropped readings are verified **only** by the pause in arrivals (no message for ~2 min at the subscriber) and by `skipped` ≥ 1 in the serial `status` output (roughly 2 for a 2-minute outage), **not** by consecutive or gapped `seq` values;
  - no old reading arrives late: the first message after reconnect is a fresh sample, received within one interval of the reconnect.
- After a reset, `boot_id` changes and `seq` restarts at 0.

**Without hardware:** compile only. Optional local host test against `mosquitto` (espMqttClient builds on Linux); not in CI.

### T7 — Serial CLI and status LED
**Owns:** `src/serial_cli.*`, `src/status_led.*`.
**Acceptance:**
- The CLI reads lines at 115200. `portal` sets the flag and reboots. `reboot`, `help`.
- `status` prints IP, RSSI, MQTT state, device_id, boot_id, seq, skipped and free heap.
- `raw` prints one fresh `soil_raw` plus the SHT31-or-DHT11/BH1750 values and names the temperature/humidity sensor in use; `status` names it too.
- `soilcheck` runs the guided §7.3 check without blocking telemetry:
  - Prompts "hold the sensor in AIR, press Enter", then streams `soil_raw` every 1 s for 20 s.
  - Prompts "put it in WATER up to the line, press Enter", then streams for 30 s.
  - Records min/max/last and the time to settle within ±30 counts of the final value.
  - Prints the `judgeSoilCheck` verdict with its plain-language fix from §7.3. Nothing is saved or published.
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
- 24 h soak with the Pi, logging `mosquitto_sub` to a file. Count **unique** `(boot_id, seq)` keys, not raw lines, and expect:
  - ≥99 % of the expected 1440 unique keys;
  - no unplanned reboot (`boot_id` constant);
  - transport-level duplicates allowed only if the payload is byte-identical (a repeated key with a different payload is a failure);
  - the gateway's `readings` table has exactly **one row per key**: `select boot_id, seq, count(*) from readings where device_id = … group by 1,2 having count(*) > 1` returns no rows.
- Unplug the SHT31 VIN mid-run and check for `null` and then recovery.
- Cross-repo: the gateway inserts the messages, and the dashboard calibration screen shows live `soil_raw`.

### What can be tested without hardware
- All of `olvar_core` (T1–T3) with native Unity tests in CI.
- A compile check of every Arduino adapter, also in CI.
- The contract fixtures (T2), which the gateway can reuse.
- The **gateway** can be developed without the ESP32 using `tools/sim_telemetry.sh` (T2). It keeps one `boot_id` per simulated boot and increments `seq` on every message, so dedup on `(device_id, boot_id, seq)` doesn't drop the stream. Republishing the static example JSON would make every message a duplicate after the first.

---

## 7. Manual setup steps

### 7.1 Installs (your Mac)
1. Install PlatformIO Core: `brew install platformio`, `pipx install platformio`, or the VS Code "PlatformIO IDE" extension. Native tests also need `xcode-select --install`. Guide: https://docs.platformio.org/en/latest/core/installation/index.html
2. Connect the ESP32 with a **data** USB cable. If no `/dev/cu.*` port appears, install the USB-UART driver for the chip near the USB socket (CP2102 or CH340).

### 7.2 Raspberry Pi 3B (details in the gateway plan; the firmware needs these)
1. Give the Pi a **DHCP reservation** in your router and note its IP.
2. Install Mosquitto. Create one user for the device whose **username is exactly the device id** the ESP32 prints on its first boot: `sudo mosquitto_passwd /etc/mosquitto/passwd olvar-a1b2c3`. The gateway's ACL only lets that username write under `olvar/v1/olvar-a1b2c3/…`. If you change the id in the portal, it must match `^[a-z0-9][a-z0-9-]{2,31}$` and must not be `olvar-gateway`, and you must create the matching broker user.

### 7.3 Soil sensor: safe wiring and software check (no chip reading, no multimeter)

**Your model:** a generic capacitive soil moisture sensor, 3.3–5.5 V. The seller's manual says a built-in regulator and 0–3.0 V output; the version isn't stated (§2.3). Its three pins are **VCC**, **GND** and **AOUT**. The usual cable colours are red = VCC, black = GND, yellow = AOUT, but not every board uses them, so **check the pin labels printed on your board next to the connector, or its datasheet, before applying power** and follow the labels, not the colours.

**Safe default wiring (always use this):**
- **VCC → 3V3**: the board's 3V3 pin, or any expansion-board V pin with the jumper on 3.3 V. **Never 5V or VIN.** If your board happens to lack the regulator, 5 V could push AOUT above 3.3 V and damage GPIO34. At 3.3 V that can't happen, and a board with a regulator works fine on 3.3 V.
- **GND → GND**.
- **AOUT → GPIO34**, plus (recommended) a 100 nF capacitor from GPIO34 to GND (§7.4).
- Don't put it in the pot yet. Before the check, wipe the blade dry.

**Software check** (after flashing; takes ~2 minutes):
1. Run `pio device monitor` and type `soilcheck`, then Enter.
2. When asked, **hold the sensor in the air** by its top edge, not touching the blade. Press Enter and wait 20 s.
3. When asked, **stand the blade in a glass of water up to the white line**. Keep the electronics dry. Press Enter and wait 30 s.
4. The firmware prints the air value, the water value, the span (air − water) and a verdict.

**What to expect** (12-bit counts, 3.3 V supply): air **≈ 2400–3900**, water **≈ 900–2300**, span **≥ 600** (usually 1000–2000). Higher = drier. The exact numbers differ per sensor; the dashboard calibration uses your own values.

| Verdict (checked in this order) | Rule | What it means → what to do |
|---|---|---|
| **Saturated** | air min ≥ 4000 | The pin sees ≥ ~3.1 V. **Unplug the VCC wire now.** Make sure it's on **3V3**, not 5V/VIN, and that AOUT (not VCC) goes to GPIO34. Then re-run. |
| **NoSignal** | air max < 300 | Nothing reaches GPIO34, or the sensor has no power. Re-seat all three wires and check that AOUT is on **GPIO34** (not 35/32) and VCC/GND aren't swapped. Re-run. |
| **Noisy** | max − min > 300 within the air phase | The value jumps around: a loose wire or a floating pin (an unconnected ADC pin reads random values). Push the AOUT wire in firmly, check the 100 nF capacitor sits between GPIO34 and GND, use shorter wires, re-run. |
| **Swapped** | water value > air value + 150 | The steps were probably done in the wrong order. Re-run: air first, then water. |
| **Stuck** | span < 150 | The sensor doesn't react to water. Its oscillator isn't running, which is typical of boards with a 5 V-only timer chip, or it's a dead board. **Replace the sensor**; they're about $1–2. When reordering, choose a listing whose photos show "TLC555" on the chip, or a branded one (DFRobot SEN0193). |
| **Slow** | water value not settled (within ±30) after 15 s, or still drifting at 30 s | The reading follows water very slowly: the known "floating 1 MΩ resistor" defect. Fix without soldering: plug a **1 MΩ resistor** (colour bands brown-black-green) into the breadboard **between the AOUT row and a GND row**. Re-run; it should now settle in a few seconds. If it's still slow, replace the sensor. |
| **SmallSpan** | 150 ≤ span < 600 | It reacts, but weakly. Make sure the blade is in water **up to the line** and was **dry** for the air step, then re-run. If the span stays small it still works, but with coarse resolution; replace when convenient. |
| **OK** | everything else (span ≥ 600) | Good. Dry the sensor, then insert it into the pot up to the line, always at the same depth and spot. |

After a passing check, protect the sensor: coat the cut PCB edges along the blade and the electronics at the top (nail varnish, PlastiDip or heat-shrink). **Never push it in past the white line.** Then calibrate in the dashboard (§7.6).

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
| 11 | Soil sensor **VCC** (often red) | **3V3** | |
| 12 | Soil sensor **GND** (often black) | **GND** | |
| 13 | Soil sensor **AOUT** (often yellow) | **GPIO34** | ADC1_CH6, input-only; **not** an ADC2 pin |
| 14 | 100 nF ceramic capacitor ("104"), recommended | between **GPIO34** and **GND**, close to the ESP32 | ADC noise; optional thanks to the 64-sample filter |
| 15 | USB cable | ESP32 board's USB port → 5 V ≥1 A charger (or your Mac while flashing) | |

**Pins not to use for sensors:** GPIO0/2/5/12/15 (boot strapping), GPIO6–11 and 16–17 (flash), and all ADC2 pins for analog.

**No SHT31?** A bare 4-pin DHT11 works instead: pin 1 VDD → 3V3, pin 2 DATA → GPIO4, pin 3 NC, pin 4 GND (grille facing you, pins down, left to right), 10 kΩ between DATA and 3V3. A 2×3-header SHT31 board must not go into one side of a breadboard (pins in the same row short). Details in `docs/WIRING.md`.

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

## 8. Risks and open questions

### Open questions
The final decisions answer the earlier questions (board, Wi-Fi, OTA, soil scale, interval). **No firmware question is still open.** The listing doesn't say which version of the soil sensor you'll receive, so whether it works is only known after the `soilcheck` in §7.3 (no tools needed). If the verdict is **Stuck**, get a replacement before T4's hardware verification.

### Risks
| Risk | Likelihood | Mitigation |
|---|---|---|
| Faulty cheap soil sensor (5 V-only timer / floating R4 / missing regulator); the listing can't rule these out | High | 3V3 wiring is safe for every variant. The `soilcheck` verdict says what to do: plug in a 1 MΩ resistor for **Slow**, replace for **Stuck**. |
| Soil sensor drifts over months as moisture gets into its edges | Medium | Coat the edges. Re-calibrate in the dashboard. `soil_raw` is always stored. |
| Dry-air reading sits near the ADC's non-linear top (>2.45 V at 11 dB) | Medium | The server-side two-point calibration absorbs it. Readings only clip at 4095 (~3.1 V). |
| Calibrating at a 60 s interval is slow: one reading per minute | Medium | §7.6 says to wait ≥2 readings. The dashboard should show the age of the latest `soil_raw`. |
| Temperature reads high because the ESP32 or charger warms the SHT31 | Medium | Place it 15–20 cm away. |
| DHT11 used instead of the SHT31: whole-degree / whole-percent steps, ±2 °C / ±5 %RH, rated 20–90 %RH | Medium | Same telemetry fields; charts look steppy. Swap back to an SHT31 when one is available (it wins automatically at boot). |
| I2C errors from breadboard jumper wires | Medium | 100 kHz, short wires, bus recovery, `null` fields |
| QoS 1 resend after reconnect would arrive late and get a stale gateway timestamp | Low | Discard unacked telemetry on disconnect (T6). The gateway also dedupes on `(device_id, boot_id, seq)`. |
| QoS 1 retransmit while connected (PUBACK later than 10 s) delivers a duplicate | Low | Expected at-least-once behaviour. The gateway stores the first copy (dedup key). Acceptance checks count unique keys. |
| PlatformIO 8.0.0 is only 4 weeks old | Low–Medium | Exact pin. Fallback to pioarduino `55.03.312-1` (same core). |
| Unencrypted MQTT on the LAN (accepted in decision F) | Low (home LAN) | Per-device credentials; broker not exposed outside the LAN |
| USB power from the Pi browns out the ESP32 | Low | Use a separate ≥1 A charger. |
| espMqttClient reconnect edge cases | Low | App-level backoff plus the 10-minute no-publish restart |
