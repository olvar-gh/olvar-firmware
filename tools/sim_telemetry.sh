#!/usr/bin/env bash
# Simulates one Olvar ESP32 so the gateway can be developed without hardware.
# Publishes the telemetry contract (docs/PLAN.md §4.1, docs/contract/telemetry.schema.json)
# to olvar/v1/<DEVICE>/telemetry with QoS 1, logged in as the device user.
#
# Usage:
#   HOST=… DEVICE=… PASS=… [PORT=1883] [INTERVAL=60] [REBOOT_EVERY=0] [NULL_EVERY=0] [COUNT=0] \
#     [CLIMATE=sht31] \
#     tools/sim_telemetry.sh
#
#   REBOOT_EVERY=n  every n messages: new boot_id, seq and uptime_ms restart at 0
#   NULL_EVERY=n    every nth message has temperature_c = null (sensor read failure)
#   COUNT=n         stop after n messages (0 = run until Ctrl-C)
#   CLIMATE=name    climate_sensor value: sht31 (default), dht11, or none to omit the field
#
# Needs mosquitto_pub (brew install mosquitto / apt install mosquitto-clients) and openssl.
# Note: PASS is passed to mosquitto_pub on its command line, so other local users can see it in ps.
set -euo pipefail

: "${HOST:?set HOST to the broker host}"
: "${DEVICE:?set DEVICE to the device id (= MQTT username)}"
: "${PASS:?set PASS to the device MQTT password}"
PORT="${PORT:-1883}"
INTERVAL="${INTERVAL:-60}"
REBOOT_EVERY="${REBOOT_EVERY:-0}"
NULL_EVERY="${NULL_EVERY:-0}"
COUNT="${COUNT:-0}"
FW="${FW:-0.1.0+sim0000}"
CLIMATE="${CLIMATE:-sht31}"
case "$CLIMATE" in
  sht31 | dht11) climate_json=",\"climate_sensor\":\"$CLIMATE\"" ;;
  none) climate_json="" ;;
  *)
    echo "CLIMATE must be sht31, dht11 or none" >&2
    exit 2
    ;;
esac

if ! [[ "$DEVICE" =~ ^[a-z0-9][a-z0-9-]{2,31}$ ]] || [[ "$DEVICE" == "olvar-gateway" ]]; then
  echo "DEVICE must match ^[a-z0-9][a-z0-9-]{2,31}\$ and not be olvar-gateway" >&2
  exit 2
fi
for n in PORT INTERVAL REBOOT_EVERY NULL_EVERY COUNT; do
  if ! [[ "${!n}" =~ ^[0-9]+$ ]]; then
    echo "$n must be a non-negative integer" >&2
    exit 2
  fi
done

TELEMETRY_TOPIC="olvar/v1/$DEVICE/telemetry"
STATUS_TOPIC="olvar/v1/$DEVICE/status"

pub() {
  # $1 = topic, $2 = payload, rest = extra mosquitto_pub flags
  local topic="$1" payload="$2"
  shift 2
  mosquitto_pub -h "$HOST" -p "$PORT" -i "$DEVICE" -u "$DEVICE" -P "$PASS" -q 1 "$@" -t "$topic" -m "$payload"
}

new_boot() {
  boot_id="$(openssl rand -hex 4)"
  seq=0
  boot_started=$SECONDS
  echo "boot_id=$boot_id" >&2
}

# Random walk of an integer in hundredths, clamped to [lo, hi].
walk() {
  local value="$1" step="$2" lo="$3" hi="$4"
  value=$((value + RANDOM % (2 * step + 1) - step))
  ((value < lo)) && value=$lo
  ((value > hi)) && value=$hi
  echo "$value"
}

# Hundredths -> decimal string with 2 dp (values here are always >= 0).
centi() { printf '%d.%02d' $(($1 / 100)) $(($1 % 100)); }

shutdown() {
  trap - INT TERM
  pub "$STATUS_TOPIC" offline -r || true
  echo "published offline" >&2
  exit 0
}
trap shutdown INT TERM

temp=2300 hum=5000 lux=30000 soil=2200 # temp/hum/lux in hundredths
sent=0
new_boot
pub "$STATUS_TOPIC" online -r

while :; do
  temp=$(walk "$temp" 15 1500 3500)
  hum=$(walk "$hum" 50 2000 9000)
  lux=$(walk "$lux" 2000 0 2000000)
  soil=$(walk "$soil" 10 1200 3000)
  rssi=$((-50 - RANDOM % 26))
  uptime_ms=$(((SECONDS - boot_started) * 1000))

  temp_json="$(centi "$temp")"
  if ((NULL_EVERY > 0)) && (((sent + 1) % NULL_EVERY == 0)); then
    temp_json=null
  fi
  lux_tenths=$(((lux + 5) / 10))
  payload=$(printf '{"device_id":"%s","boot_id":"%s","seq":%d,"uptime_ms":%d,"temperature_c":%s,"humidity_pct":%s,"lux":%d.%d,"soil_raw":%d%s,"rssi_dbm":%d,"fw":"%s"}' \
    "$DEVICE" "$boot_id" "$seq" "$uptime_ms" "$temp_json" "$(centi "$hum")" \
    $((lux_tenths / 10)) $((lux_tenths % 10)) "$soil" "$climate_json" "$rssi" "$FW")

  pub "$TELEMETRY_TOPIC" "$payload"
  echo "$payload"
  seq=$((seq + 1))
  sent=$((sent + 1))

  if ((COUNT > 0 && sent >= COUNT)); then
    shutdown
  fi
  if ((REBOOT_EVERY > 0 && seq >= REBOOT_EVERY)); then
    new_boot
    pub "$STATUS_TOPIC" online -r
  fi
  sleep "$INTERVAL" &
  wait $! # so Ctrl-C is handled immediately, not after the sleep
done
