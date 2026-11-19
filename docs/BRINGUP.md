# Bring-up checklist

From a bare board to telemetry arriving on the Raspberry Pi, step by step. Tick each box before
moving on; every step says what you should see and what to do if you don't.

Related docs: [WIRING.md](WIRING.md) (wiring and the soil sensor check),
[PROVISIONING.md](PROVISIONING.md) (the setup page in detail), [PLAN.md](PLAN.md) §7.

## 0. What you need

- [ ] The ESP32 board on its expansion board, the SHT31 (or a DHT11 plus a 10 kΩ resistor, see
      [WIRING.md](WIRING.md#dht11-instead-of-the-sht31)), the GY-302 (BH1750), the soil sensor,
      jumper wires and the breadboard. A 100 nF capacitor is recommended.
- [ ] A **data** USB cable (some cables only charge; if no port shows up in step 2, try another).
- [ ] Your Mac with PlatformIO Core installed (`brew install platformio`) and the command line
      tools (`xcode-select --install`).
- [ ] The Raspberry Pi with Mosquitto running on port 1883, and its IP address (give it a DHCP
      reservation in your router so the address never changes).
- [ ] A phone that can join a Wi-Fi network.

## 1. Wiring check (USB unplugged)

Do the wiring exactly as in [WIRING.md](WIRING.md), then check:

- [ ] The expansion board's V-rail jumper is on **3.3 V**.
- [ ] Every sensor's VCC/VIN goes to **3V3**. Nothing goes to 5V or VIN.
- [ ] Every GND goes to GND.
- [ ] SHT31 SDA and GY-302 SDA both go to **GPIO21**; both SCL go to **GPIO22**. A 2×3-pin
      SHT31 board sits across the breadboard's centre gap (or on jumper wires), never in one side.
- [ ] Or, with a DHT11 instead of the SHT31: pin 1 → 3V3, pin 2 → **GPIO4**, pin 4 → GND, pin 3
      free, and a 10 kΩ resistor between pin 2 and 3V3.
- [ ] Soil sensor: red → 3V3, black → GND, yellow → **GPIO34**.
- [ ] If fitted (recommended), the 100 nF capacitor sits between GPIO34 and GND.
- [ ] No bare wire ends touch each other.

If anything is unsure, stop and fix it now: a wrong power wire is the only mistake here that can
damage a part.

## 2. Flash the firmware over USB

1. Plug the board into the Mac.
2. Check the board shows up:
   ```sh
   pio device list
   ```
   - [ ] You see a `/dev/cu.usbserial-…` or `/dev/cu.SLAB_USBtoUART` / `/dev/cu.wchusbserial…` port.
   - Nothing? Try another (data) cable. Still nothing: install the USB driver for the chip next to
     the USB socket (CP2102 → Silicon Labs driver, CH340 → WCH driver), then replug.
3. In this folder, build and flash, then open the serial monitor:
   ```sh
   pio run -e esp32dev -t upload && pio device monitor
   ```
   - [ ] The upload ends with `Hard resetting via RTS pin...` and `[SUCCESS]`.
   - Stuck at `Connecting........`? Hold the **BOOT** button until the upload starts, then
     release it.
   - Leave the monitor open from now on. To quit it later press `Ctrl+C`.
4. Press **EN** (reset) once. You should see:
   ```
   olvar-firmware 0.1.0+a1b2c3d
   device_id olvar-a1b2c3
   [portal] device_id olvar-a1b2c3
   [portal] join Wi-Fi "olvar-setup-a1b2c3" with password "k7xq3mvh2p", then open http://192.168.4.1
   ```
   - [ ] Write down the **device_id** (`olvar-xxxxxx`) and the setup **password**.

On a fresh board the setup page always opens first, because no Wi-Fi network is saved yet.

## 3. Create the device's broker user on the Pi

The MQTT username must be exactly the device_id from step 2. On the Pi:

```sh
sudo mosquitto_passwd /etc/mosquitto/passwd olvar-a1b2c3
sudo systemctl reload mosquitto
```

- [ ] Choose a password and keep it for step 4. (Details are in the gateway's own plan.)

## 4. Wi-Fi setup page (phone)

1. On the phone, join the Wi-Fi network **`olvar-setup-xxxxxx`** with the password from step 2.
   The setup page usually opens by itself; otherwise open `http://192.168.4.1` in the browser.
2. Tap **Configure WiFi**, pick your home network (it must be 2.4 GHz) and type its password.
3. Fill the fields below it:
   - **Device id**: leave it as it is.
   - **MQTT host**: the Pi's IP address, e.g. `192.168.1.20`.
   - **MQTT port**: `1883`.
   - **MQTT password**: the password from step 3.
4. Tap **Save**.
   - [ ] The serial monitor prints `[portal] saved; restarting as olvar-xxxxxx` and the board
         restarts.
   - A red "Not saved: …" line on the page tells you which field to fix.
   - "Could not join that Wi-Fi network": check the network name and password, and that it is
     2.4 GHz.

To change these settings later, hold **BOOT** for 3 s while the board is running and release it, or
type `portal` in the serial monitor. Never hold BOOT while pressing EN: that starts flashing mode.

## 5. First normal boot

After the restart the monitor shows something like:

```
olvar-firmware 0.1.0+a1b2c3d
device_id olvar-a1b2c3
boot_id 9f3c07ab
[sensors] temperature/humidity: SHT31 at 0x44
[mqtt] broker 192.168.1.20:1883, user olvar-a1b2c3
Type 'help' for commands.
[wifi] connected, ip 192.168.1.57, rssi -61 dBm
[mqtt] connected, olvar/v1/olvar-a1b2c3/status = online
[telemetry] sent seq 0, heap 187312
```

- [ ] `[sensors] temperature/humidity:` names your sensor (`SHT31 at 0x44`, or `DHT11 on GPIO4`).
      `[sensors] no SHT31 … or DHT11 … found` or `[sensors] BH1750 not found` means that sensor's
      wires need a recheck (step 1).
- [ ] `[wifi] connected` appears within ~10 s.
- [ ] `[mqtt] connected` follows, and the first `[telemetry] sent seq 0` comes ≤ 5 s later. After
      that, one `[telemetry] sent` line every 60 s with `seq` counting up.

The blue LED on the board shows the state:

| LED | Meaning | What to check |
|---|---|---|
| slow blink (1 s) | no Wi-Fi | network name/password, 2.4 GHz, distance to the router |
| fast blink | Wi-Fi OK, no MQTT | Pi IP address, Mosquitto running, username = device_id, password |
| off, short flash every 60 s | online, telemetry sent | all good |

## 6. Check the board from the serial monitor

Type these commands in the monitor and press Enter:

- [ ] `status`: `wifi connected`, `mqtt connected`, `seq` growing, `skipped 0`, and `climate`
      shows which temperature/humidity sensor is in use.
- [ ] `raw`: one line like
      `soil_raw=3120 temperature_c=23.41 humidity_pct=51.07 lux=312.5 (temp/humidity: SHT31 at 0x44)`.
  - SHT31: temperature 5–40 °C and humidity 10–95 %. DHT11: 0–50 °C and 20–90 %, usually in
    whole numbers (`23.00`). Within 2 s of the last DHT11 read, `raw` repeats that read.
  - Lux above 0 in room light and ≤ 5 when you cover the GY-302 with your finger: the BH1750 works.
  - `null` for a value means that sensor did not answer: recheck its wires.
- [ ] `soilcheck`: follow the prompts (sensor in the air, then in a glass of water up to the white
      line). It ends with a verdict. **OK** means the soil sensor is good; any other verdict prints
      what to do. The full table is in [WIRING.md](WIRING.md). Telemetry keeps running during
      the check; nothing is saved or sent.

`help` lists every command.

## 7. See telemetry arrive on the Pi

On the Pi (or any computer with `mosquitto_sub`), with a broker user that may read `olvar/v1/#`:

```sh
mosquitto_sub -h 192.168.1.20 -u <admin-user> -P '<admin-password>' -v -q 1 -t 'olvar/v1/#'
```

- [ ] Right away: `olvar/v1/olvar-a1b2c3/status online` (it is retained, so it shows at once).
- [ ] Every 60 s: a telemetry line like
  ```
  olvar/v1/olvar-a1b2c3/telemetry {"device_id":"olvar-a1b2c3","boot_id":"9f3c07ab","seq":42,"uptime_ms":2520311,"temperature_c":23.41,"humidity_pct":51.07,"lux":312.5,"soil_raw":2210,"rssi_dbm":-61,"fw":"0.1.0+a1b2c3d"}
  ```
- [ ] `boot_id` stays the same and `seq` goes up by one each minute.
- [ ] Unplug the board's USB: within about 45 s the status changes to `offline`.

Once this works, place the soil sensor in the pot and calibrate it in the dashboard
(PLAN.md §7.6).

## 8. Release checks for 0.1.0

These confirm the firmware on real hardware. Run them once before calling the release done.

**One sensor unplugged.** While telemetry is flowing, pull the SHT31's VIN wire. With a DHT11,
pull the wire from its pin 2 (DATA) row to **GPIO4** instead and leave it powered: without power,
the pull-up on DATA would feed the sensor through its data pin.
- [ ] The next messages have `"temperature_c":null,"humidity_pct":null`; `lux` and `soil_raw` are
      still numbers.
- [ ] Plug the wire back in: within a minute or two the values return, without a reboot (`boot_id`
      unchanged).

**Broker outage.** On the Pi: `sudo systemctl stop mosquitto`, wait 2 minutes, then
`sudo systemctl start mosquitto`.
- [ ] The board reconnects by itself (`boot_id` unchanged) and the first message after the
      reconnect arrives within a minute.
- [ ] The serial `status` shows `skipped` ≥ 1 (about 2 for a 2-minute outage).

**24 h soak.** Leave the board running for 24 hours and log everything on the Pi:

```sh
mosquitto_sub -h localhost -u <admin-user> -P '<admin-password>' -q 1 \
  -t 'olvar/v1/olvar-a1b2c3/telemetry' > soak.log
```

Afterwards (needs `jq`: `sudo apt install jq`):

```sh
jq -r '"\(.boot_id) \(.seq)"' soak.log | sort -u | wc -l                 # unique keys
jq -r .boot_id soak.log | sort -u                                        # boot ids
sort -u soak.log | jq -r '"\(.boot_id) \(.seq)"' | sort | uniq -d         # conflicts
grep -ci nan soak.log                                                     # NaN on the wire
```

- [ ] Unique keys ≥ 1426 (99 % of the 1440 expected).
- [ ] Exactly one boot id (no unplanned reboot).
- [ ] The conflicts command prints nothing: a repeated `(boot_id, seq)` is fine only with a
      byte-identical payload.
- [ ] The NaN count is 0.
- [ ] In the gateway database, this returns no rows:
      `select boot_id, seq, count(*) from readings where device_id = 'olvar-a1b2c3' group by 1,2 having count(*) > 1;`
- [ ] Free heap stays steady: compare the `heap` number in `status` (or in the
      `[telemetry] sent … heap …` lines) at the start and the end of the soak. It may move by a
      few kB but must not keep falling.
- [ ] The dashboard's soil calibration screen shows the live `soil_raw`.

## 9. Updating the firmware later

Plug the board into the Mac and run `pio run -e esp32dev -t upload`. The settings survive an
upload. Only `pio run -e esp32dev -t erase` clears them (the setup page then opens again).
