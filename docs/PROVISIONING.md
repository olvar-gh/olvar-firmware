# Provisioning

How the board gets its Wi-Fi and MQTT settings, where they are stored, and how to change them.
Code: `src/storage/settings_store.*` (NVS) and `src/net/provisioning.*` (portal, BOOT button).

## What is stored

Settings live in NVS, Preferences namespace `olvar`:

| Key       | Field       | Notes |
|-----------|-------------|-------|
| `dev_id`  | `device_id` | Also the MQTT client id and MQTT username. Rule `^[a-z0-9][a-z0-9-]{2,31}$`, `olvar-gateway` is reserved. |
| `mq_host` | MQTT host   | IP address or host name (letters, digits, `.`, `-`). `*.local` is resolved by the MQTT link. |
| `mq_port` | MQTT port   | 1-65535, default 1883. |
| `mq_pass` | MQTT password | Per-device password from the gateway's `mosquitto_passwd`. |
| `portal`  | portal flag | Present (true) only while the portal has been requested. |

There is no username key: the MQTT username is always `device_id`.
The Wi-Fi network and its password are stored by the ESP32 Wi-Fi driver, not in `olvar`.

A key that is missing (or unreadable) falls back to, in order:
1. the `OLVAR_DEFAULT_*` build flag from `secrets.ini`, when set and non-empty;
2. the built-in default: `device_id` = `olvar-` + last 3 MAC bytes (e.g. `olvar-a1b2c3`), port 1883,
   empty host and password.

`secrets.ini` is for development only and is gitignored. Values saved in the portal always win.

## When the portal opens

At boot the portal opens if any of these is true:
- no Wi-Fi network has been saved (fresh or erased flash);
- the `portal` flag is set;
- the stored settings fail validation (e.g. empty MQTT host), so the board could not run anyway.

The flag is set by:
- holding **BOOT** for at least 3 s while the board is running, then releasing it;
- the `portal` serial command.

The board then restarts into the portal. BOOT is only read while the firmware runs. Don't hold
BOOT while pressing EN/RST: GPIO0 low at reset starts the ROM bootloader (flashing mode), which is
also why the long-press fires on release, not after 3 s.

## Using the portal

1. Open the serial monitor (`pio device monitor`, 115200). The board prints:
   ```
   [portal] device_id olvar-a1b2c3
   [portal] join Wi-Fi "olvar-setup-a1b2c3" with password "k7xq3mvh2p", then open http://192.168.4.1
   ```
   The AP password is random for each portal session.
2. On a phone, join `olvar-setup-xxxxxx` with that password. The setup page usually opens by
   itself; otherwise browse to `http://192.168.4.1`.
3. Tap **Configure WiFi**, pick your 2.4 GHz network and enter its password.
4. Check the fields below it. They are pre-filled with the current settings:
   - **Device id (MQTT username)**: keep the default unless the gateway user is named differently.
     Upper case is turned into lower case.
   - **MQTT host**: the Raspberry Pi's IP address (or its name).
   - **MQTT port**: 1883.
   - **MQTT password**: the device's password on the gateway. The stored password is never shown;
     leave the field empty to keep it.
5. Tap **Save**. The board joins the network, stores the settings, clears the flag and restarts in
   normal mode.

To change only the MQTT fields later, open the portal with a long-press, leave the network name
empty, edit the fields and save: the saved Wi-Fi network is kept.

If a field is invalid, nothing is saved and the browser goes straight back to the form, which
shows **Not saved: ...** with the broken rule (for example `MQTT port must be 1-65535`). The same
happens if no network is chosen and none is saved, or if NVS cannot be written. Fix it and save again.

When a network is submitted, the board answers with WiFiManager's "trying to connect" page and then
tries to join. If joining fails, the portal stays open and the form shows **Could not join that
Wi-Fi network**. If the network was joined but NVS could not be written, the board restarts straight
back into the portal (the Wi-Fi network is kept); the reason is printed on serial.

The portal menu has no **Exit** or **Erase**. If the portal is stopped anyway (for example by
opening `/exit`), the board restarts at once, following the timeout rule below.

## Timeout

The portal closes after 5 minutes without a phone connected to its AP, then restarts:
- into normal mode if a Wi-Fi network is saved and the stored settings are valid (the flag is cleared);
- back into the portal otherwise.

## Starting over

`pio run -e esp32dev -t erase` erases the whole flash (settings and Wi-Fi). Flash again and the
portal opens on first boot.

## Integration notes (for the app)

```cpp
olvar::NvsKvStore kv;
olvar::SettingsStore store(kv);
Settings settings;
uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
store.load(settings, olvar::defaultSettings(mac));

olvar::Provisioning portal(store);
if (portal.needed(settings)) portal.run(settings);   // never returns

olvar::BootButton boot; boot.begin();
// in loop(): if (boot.poll()) { store.setPortalRequested(true); /* publish offline */ ESP.restart(); }
```
