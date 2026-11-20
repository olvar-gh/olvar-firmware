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
   - **MQTT host**: the Pi's IP address, e.g. `192.168.1.20`. Give the Pi a DHCP reservation in
     the router first, so that address never changes; this is the most robust setting (see
     [§11](#11-link-drops-and-the-link-record)). A name like `olvar-gw.local` works too, but it
     depends on mDNS answers reaching the board.
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
  - SHT31: temperature 5–40 °C and humidity 10–95 %. DHT11: 0–50 °C and, in a room, 20–60 %,
    usually in whole numbers (`23.00`). Within 2 s of the last DHT11 read, `raw` repeats that
    read. With a DHT11 (or no sensor found) `raw` prints a second `dht11 …` line with the raw
    bytes; [§10](#10-humidity-check-dht11) explains it.
  - Lux above 0 in room light and ≤ 5 when you cover the GY-302 with your finger: the BH1750 works.
  - `null` for a value means that sensor did not answer: recheck its wires.
- [ ] `soilcheck`: follow the prompts (sensor in the air, then in a glass of water up to the white
      line). It ends with a verdict. **OK** means the soil sensor is good; any other verdict prints
      what to do. The full table is in [WIRING.md](WIRING.md). Telemetry keeps running during
      the check; nothing is saved or sent.

- [ ] With a DHT11: `dhtwatch` and the breath test in [§10](#10-humidity-check-dht11).

`help` lists every command.

## 7. See telemetry arrive on the Pi

On the Pi (or any computer with `mosquitto_sub`), with a broker user that may read `olvar/v1/#`:

```sh
mosquitto_sub -h 192.168.1.20 -u <admin-user> -P '<admin-password>' -v -q 1 -t 'olvar/v1/#'
```

- [ ] Right away: `olvar/v1/olvar-a1b2c3/status online` (it is retained, so it shows at once).
- [ ] Every 60 s: a telemetry line like
  ```
  olvar/v1/olvar-a1b2c3/telemetry {"device_id":"olvar-a1b2c3","boot_id":"9f3c07ab","seq":42,"uptime_ms":2520311,"temperature_c":23.41,"humidity_pct":51.07,"lux":312.5,"soil_raw":2210,"climate_sensor":"sht31","rssi_dbm":-61,"fw":"0.1.0+a1b2c3d"}
  ```
- [ ] `boot_id` stays the same and `seq` goes up by one each minute.
- [ ] `climate_sensor` names the sensor from step 5: `"sht31"` or `"dht11"`. It is missing only
      while the board has found neither; the dashboard then shows a neutral "Climate sensor" line.
- [ ] Unplug the board's USB: within about 25 s (1.5 × the 15 s keepalive) the status changes to
      `offline`.

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

**Wi-Fi outage.** Switch the router's 2.4 GHz radio off (or unplug the router) for 2 minutes,
then back on.
- [ ] Within about a minute of the network being back, telemetry flows again with the same
      `boot_id` (no restart). `status` shows the Wi-Fi drop under `this boot:`.

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

## 10. Humidity check (DHT11)

Run this when a DHT11 is in use and humidity looks wrong, for example stuck at 5–8 % in a room
that is really 30–50 %. It tells a worn sensor apart from a wiring, timing or supply problem.
Paste the output when you ask for help.

**1. One read with its raw bytes.** Type `raw`. With a DHT11 you get two lines:

```
soil_raw=3120 temperature_c=23.00 humidity_pct=44.00 lux=312.5 (temp/humidity: DHT11 on GPIO4)
dht11 bytes=2C 00 17 00 43 checksum=ok bit0_us=22..26 bit1_us=68..73 start_us=20110 age_ms=3
```

- `bytes`: the five bytes the sensor sent, in hex: humidity, humidity tenths, temperature,
  temperature tenths, checksum. `2C` is 44 %RH, `17` is 23 °C. The firmware prints exactly what
  is in the first and third byte; it doesn't scale, clamp or round them.
- `checksum=ok`: the fifth byte matches the sum of the first four.
- `bit0_us` / `bit1_us`: the shortest and longest high pulse, in microseconds, of the bits read
  as 0 and as 1 (the firmware splits them at 50 µs; the datasheet says 23–27 and 68–74).
- `start_us`: how long the board held the line low to wake the sensor (datasheet: 18000–30000).
- `age_ms`: how long ago that read was; within 2 s of a read, `raw` shows the same one again.

Healthy: `checksum=ok`, `bit0_us` inside about 15..35, `bit1_us` inside about 60..80, `start_us`
between 18000 and 30000, and a first byte between `14` and `3C` (20–60 %RH) in a room.

**2. Breath test.** Type `dhtwatch`. It reads every 2 s for 30 s, one line per read. After three
or four lines, breathe on the grille from about 5 cm for 2–3 s, then leave it alone.

```
   0 s  humidity_pct=44.0 temperature_c=23.0 bytes=2C 00 17 00 43 checksum=ok bit0_us=22..26 bit1_us=68..73 start_us=20108
   2 s  humidity_pct=44.0 temperature_c=23.0 bytes=2C 00 17 00 43 checksum=ok bit0_us=22..26 bit1_us=68..74 start_us=20104
   …
  10 s  humidity_pct=78.0 temperature_c=24.0 bytes=4E 00 18 00 66 checksum=ok bit0_us=22..26 bit1_us=68..73 start_us=20111
   …
dhtwatch done: 16 reads, 0 failed, humidity_pct min 44.0 max 81.0.
```

Healthy: it rises by 20 %RH or more within a few seconds of breathing and falls back towards
the start value by the end. Type `q` and Enter to stop early.

**3. Check the supply.** With the board running, measure with a multimeter between DHT11 pin 1
(VDD) and pin 4 (GND). It should read 3.2–3.4 V. The datasheet specifies the accuracy at 5 V and
warns that on 3.3 V, long or thin wires drop enough voltage to skew the reading, so keep the
DHT11's wires under 1 m and run VDD straight to a 3V3 pin. Don't move it to 5 V: DATA would then
swing to 5 V on a 3.3 V pin (see [WIRING.md](WIRING.md#dht11-instead-of-the-sht31)).

**What the results mean:**

| You see | Meaning | Do this |
|---|---|---|
| `checksum=ok`, pulse widths healthy, supply 3.2–3.4 V, humidity 5–8 % in a normal room but it jumps up when you breathe on it | The sensor itself reports a value far too low: its humidity element has drifted (dried out over a long time, or exposed to solvent, glue or foam vapours) or it is a weak part. 5 % is the lowest value an ASAIR DHT11 can report. The firmware and the wiring are fine | Rehydrate it (below). If it still reads low, replace the DHT11 or fit an SHT31 (it is used automatically at the next boot) |
| As above, but the supply is below 3.2 V | Not enough voltage at the sensor | Shorter or thicker wires, VDD straight to a 3V3 pin, then repeat steps 1–2 |
| `checksum=bad`, or `bit0_us` going above ~40 or `bit1_us` below ~60 | The signal is distorted: long wires, a loose contact, a weak pull-up | Re-seat the wires, keep them short, check the 10 kΩ (brown-black-orange) between DATA and 3V3. A 4.7 kΩ pull-up helps on longer wires |
| `error=line_low` | DATA stays low after the board lets go: no pull-up, or DATA touches GND | Check the 10 kΩ between pin 2 and 3V3, and that pin 2 and pin 4 aren't in the same row |
| `error=no_response` | The sensor never answers: no power, DATA not on GPIO4, or pins swapped | Recheck the wiring table in [WIRING.md](WIRING.md#dht11-instead-of-the-sht31) |
| `error=response_cut` or `error=bit_timeout` | The reply stopped half way: a loose contact or noise | Re-seat the sensor and wires; if it keeps happening, replace the sensor |
| `start_us` outside 18000–30000 | Board-side timing is off | Paste the output in an issue; this is a firmware problem |
| Humidity normal in `raw` but wrong on the dashboard | Not the sensor | Compare with the telemetry on the Pi (§7): `humidity_pct` there is the same number |

**Rehydrating a DHT11 that reads low.** The datasheet's recovery for a low reading is 24 h at
25 °C and 85 %RH. At home: put the unplugged sensor in a closed food box together with a bottle
cap of table salt wetted to a wet-sand paste (salt and water, not dissolved), without touching
it, for 24 h at room temperature. That holds about 75 %RH without condensation. Then rewire it,
wait a few minutes and repeat steps 1–2. A healthy DHT11 reads within about ±5 %RH of a reference
hygrometer. One that stays far too low has drifted for good: replace it.

## 11. Link drops and the link record

The board repairs its own link in stages, counted from the last telemetry message the broker
acknowledged (one is sent every 60 s):

| When | What the board does | Serial line |
|---|---|---|
| Wi-Fi down for 30 s | asks the Wi-Fi driver to connect again, then every 30 s (the core gives up after some disconnect reasons) | `[wifi] down for 30 s …, reconnecting` |
| a message unacked for 30 s | drops the MQTT session and opens a new one (1 s to 15 s apart) | `[mqtt] no PUBACK for 30 s, dropping the session` |
| broker name does not resolve | dials the last address that name resolved to and that accepted a connection (kept in NVS across restarts); restarts mDNS after every 2nd failed lookup in a row | `[mqtt] cannot resolve olvar-gw.local, trying the remembered 192.168.0.42` |
| no acked telemetry for 100 s | full Wi-Fi reconnect (new association and DHCP lease) and mDNS restart, again every 100 s | `[link] no acknowledged telemetry for 100 s, resetting Wi-Fi` |
| no acked telemetry for 10 min | restart, the last resort | `[link] no acknowledged telemetry for 600 s, restarting` |

Readings taken while the link is down are dropped, never sent late (`skipped` in `status` counts
them).

### Most robust setting

Give the Pi a **DHCP reservation** (a fixed address for its MAC) in the router, and enter that
**IP address as the MQTT host** in the setup portal (`portal` command, or hold BOOT for 3 s).
Then no name lookup is involved at all. With `olvar-gw.local` the board needs mDNS answers from
the Pi; some routers and access points drop or delay multicast, and then the name stops
resolving although the Pi answers ping. The remembered address covers that only while the Pi's
address stays the same, which again needs the reservation.

### Reading the link record

The board keeps a small record of its link state in memory that survives a restart (not a power
cut). It prints the previous boot's record right after `reset reason …` at boot, and `status`
prints the current one (`this boot:`) and the previous one (`previous boot:`):

```
previous boot:
  reset    SW (software restart); ended by: supervisor restart (no acknowledged telemetry)
  uptime   3720 s, 600 s since the last acked telemetry, 0 supervisor restart(s) in a row before
  wifi     CONNECTED, rssi -71 dBm; 1 drop(s), last reason 200 BEACON_TIMEOUT; 0 retry(s), 5 forced reset(s)
  mqtt     3 connect(s), 2 drop(s), 1 ack timeout(s), last reason TCP_DISCONNECTED
  broker   name lookup FAILING; 14 failed lookup(s), 0 fallback(s) to the remembered address
```

- **reset / ended by**: `reset` is how the described boot itself started. `ended by` says why
  the firmware restarted it: `supervisor restart` is the 10-minute last resort, `reboot command`
  and `setup portal requested` are yours. `not restarted by the firmware` means power loss, a
  crash or a watchdog; the reset reason of the boot after it (the `reset reason` line at boot,
  or `reset` under `this boot:`) says which: `PANIC`, `TASK_WDT`, `BROWNOUT`, `POWERON`.
- **uptime**: how long that boot ran and how long the last acked telemetry was ago when the
  record was last written (every second, and at the restart). `supervisor restart(s) in a row`
  above 0 means the outage outlived restarts: the problem is outside the board (router, Pi,
  name resolution).
- **wifi**: the status and signal at the end. `CONNECTED` with a failing MQTT link points at the
  network path or the broker; anything else (`DISCONNECTED`, `NO_SSID_AVAIL`, `CONNECT_FAILED`)
  at Wi-Fi. `last reason` is the 802.11 disconnect reason: `BEACON_TIMEOUT` / `NO_AP_FOUND`
  mean the board lost the access point (distance, interference, router restart), `AUTH_FAIL` /
  `4WAY_HANDSHAKE_TIMEOUT` mean it was refused (password, router busy). An RSSI below about
  -80 dBm is weak; move the board or the router. `retry(s)` count the 30-second reconnects,
  `forced reset(s)` the 100-second ones.
- **mqtt**: `connect(s)` are sessions the broker accepted, `drop(s)` sessions that ended,
  `ack timeout(s)` sessions dropped because the broker stopped acknowledging. `last reason` comes
  from the MQTT library: `TCP_DISCONNECTED` (connection lost or refused; Mosquitto down, wrong
  address or port), `MQTT_NOT_AUTHORIZED` / `MQTT_MALFORMED_CREDENTIALS` (password or user; see
  §3). Many connects with as many drops means the session keeps breaking right after it opens.
- **broker**: `name lookup FAILING` means the MQTT host name did not resolve at the end.
  `failed lookup(s)` with `0 fallback(s)` means no address was remembered yet; set an IP address
  as described above.

Copy both records when you report an outage.
