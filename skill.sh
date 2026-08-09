#!/usr/bin/env bash
# mqtt-grounding skill - talk to the "grounding" team's own MQTT broker.
#
# This is OUR broker, running on a teammate's laptop (not the classroom-wide
# one). Topics are namespaced under "grounding/" so our traffic doesn't
# collide with other teams sharing the same WiFi.
#
# Requires the mosquitto clients (mosquitto_pub / mosquitto_sub):
#   Windows  installer from https://mosquitto.org/download/ (adds them to
#            C:\Program Files\mosquitto - add that folder to PATH)
#   macOS    brew install mosquitto
#   Linux    sudo apt install mosquitto-clients
#
# Override the broker without editing this file:
#   export MQTT_HOST=192.168.0.35
#   export MQTT_PORT=1883

set -euo pipefail

MQTT_HOST="${MQTT_HOST:-192.168.0.35}"
MQTT_PORT="${MQTT_PORT:-1883}"
BASE="grounding"

# Your own board's name, so you don't retype it on every command.
# Saved by `./skill.sh name <id>`; MQTT_DEVICE in the environment wins over it.
CONFIG_FILE="${HOME}/.mqtt-grounding"

die() { echo "error: $*" >&2; exit 1; }

load_device() {
  if [ -n "${MQTT_DEVICE:-}" ]; then
    echo "$MQTT_DEVICE"
  elif [ -f "$CONFIG_FILE" ]; then
    sed -n 's/^device=//p' "$CONFIG_FILE" | head -1
  fi
}

# Commands take an explicit id, or fall back to the saved one.
resolve_device() {
  local id="${1:-}"
  [ -n "$id" ] || id="$(load_device)"
  [ -n "$id" ] || die "no device id. Pass one, or save yours: ./skill.sh name seongho"
  echo "$id"
}

# The Windows installer does not put mosquitto on PATH, and Git Bash inherits
# that, so look in the usual install locations before giving up.
need_clients() {
  if command -v mosquitto_sub >/dev/null 2>&1 && command -v mosquitto_pub >/dev/null 2>&1; then
    return 0
  fi

  local dir
  for dir in \
    "/c/Program Files/mosquitto" \
    "/c/Program Files (x86)/mosquitto" \
    "$HOME/scoop/apps/mosquitto/current" \
    "/opt/homebrew/bin" \
    "/usr/local/bin"
  do
    if [ -x "$dir/mosquitto_sub" ] || [ -x "$dir/mosquitto_sub.exe" ]; then
      PATH="$PATH:$dir"
      export PATH
      return 0
    fi
  done

  die "mosquitto_sub / mosquitto_pub not found.
  Windows  install from https://mosquitto.org/download/ (default location is detected automatically)
  macOS    brew install mosquitto
  Linux    sudo apt install mosquitto-clients"
}

usage() {
  local saved; saved="$(load_device)"
  cat <<EOF
mqtt-grounding - team "grounding" broker ${MQTT_HOST}:${MQTT_PORT}
your device: ${saved:-<not set - run: ./skill.sh name YOUR_NAME>}

  ./skill.sh name [ID]          save your board's name (no arg = show current)
  ./skill.sh check               test that the broker is reachable
  ./skill.sh devices             list boards that are online right now
  ./skill.sh watch [ID]          stream every message (or just one board's)
  ./skill.sh led [ID] on|off|toggle
  ./skill.sh sensor [ID]         stream that board's A0 readings
  ./skill.sh pub TOPIC PAYLOAD   publish anything (escape hatch)

ID defaults to your saved name, so once you run \`./skill.sh name seongho\`
you can just type \`./skill.sh led on\`. It must match DEVICE_NAME in the
board's arduino_secrets.h.

Topics (all under "${BASE}/" so we don't collide with other teams)
  ${BASE}/<id>/led/set      on | off | toggle      (you publish)
  ${BASE}/<id>/led/state    on | off               (retained)
  ${BASE}/<id>/sensor/a0    {"raw":2048,"mv":1650}
  ${BASE}/<id>/status       online | offline       (retained)

If the broker's IP changed (DHCP), override it for one shell:
  export MQTT_HOST=<new ip>
EOF
}

cmd_check() {
  need_clients
  local topic="${BASE}/_check/$$"
  echo "publishing to ${MQTT_HOST}:${MQTT_PORT} ..."
  if mosquitto_pub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "$topic" -m "hello" 2>/dev/null; then
    echo "OK - broker reachable"
  else
    echo "FAILED - broker not reachable at ${MQTT_HOST}:${MQTT_PORT}" >&2
    echo "  - are you on the same WiFi as the host laptop?" >&2
    echo "  - is the host laptop actually running: mosquitto -c broker/mosquitto.conf -v ?" >&2
    echo "  - has its IP changed? check with ipconfig / ifconfig and export MQTT_HOST=<ip>" >&2
    echo "  - is Windows Firewall on the host laptop blocking TCP ${MQTT_PORT}?" >&2
    exit 1
  fi
}

cmd_devices() {
  need_clients
  echo "boards reporting in (2s, Ctrl-C to stop early):"
  # Retained status messages arrive immediately on subscribe.
  mosquitto_sub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "${BASE}/+/status" -v -W 2 2>/dev/null |
    while read -r topic payload; do
      id="${topic#${BASE}/}"; id="${id%/status}"
      printf '  %-12s %s\n' "$id" "$payload"
    done || true
}

cmd_name() {
  local id="${1:-}"
  if [ -z "$id" ]; then
    local saved; saved="$(load_device)"
    if [ -n "$saved" ]; then
      echo "your device: ${saved}"
      [ -n "${MQTT_DEVICE:-}" ] && echo "(from MQTT_DEVICE in the environment)"
    else
      echo "no device saved yet - run: ./skill.sh name YOUR_NAME"
    fi
    return
  fi

  # Topic level, so keep it free of MQTT wildcards and separators.
  case "$id" in
    */*|*'#'*|*'+'*|*' '*) die "device name must not contain / # + or spaces" ;;
  esac

  printf 'device=%s\n' "$id" > "$CONFIG_FILE"
  echo "saved: ${id}  (${CONFIG_FILE})"
  echo "this must match DEVICE_NAME in your board's arduino_secrets.h"
}

cmd_watch() {
  need_clients
  local id="${1:-}"
  [ -n "$id" ] || id="$(load_device)"
  [ -n "$id" ] || id="+"          # nothing saved: watch every board
  echo "watching ${BASE}/${id}/# (Ctrl-C to stop)"
  mosquitto_sub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "${BASE}/${id}/#" -v
}

cmd_led() {
  need_clients
  local id state
  # Accept both `led on` (saved device) and `led c6-85ef58 on`.
  case "${1:-}" in
    on|off|toggle) id="$(resolve_device)"; state="$1" ;;
    *)             id="$(resolve_device "${1:-}")"; state="${2:-}" ;;
  esac
  [ -n "$state" ] || die "usage: ./skill.sh led [ID] on|off|toggle"
  case "$state" in
    on|off|toggle) ;;
    *) die "state must be on, off or toggle" ;;
  esac
  mosquitto_pub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "${BASE}/${id}/led/set" -m "$state"
  echo "sent '${state}' to ${id}"
}

cmd_sensor() {
  need_clients
  local id; id="$(resolve_device "${1:-}")"
  echo "streaming ${BASE}/${id}/sensor/a0 (Ctrl-C to stop)"
  mosquitto_sub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "${BASE}/${id}/sensor/a0"
}

cmd_pub() {
  need_clients
  local topic="${1:-}" payload="${2:-}"
  [ -n "$topic" ] || die "usage: ./skill.sh pub TOPIC PAYLOAD"
  mosquitto_pub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "$topic" -m "$payload"
  echo "published to ${topic}"
}

case "${1:-help}" in
  name)    shift; cmd_name "$@" ;;
  check)   shift; cmd_check "$@" ;;
  devices) shift; cmd_devices "$@" ;;
  watch)   shift; cmd_watch "$@" ;;
  led)     shift; cmd_led "$@" ;;
  sensor)  shift; cmd_sensor "$@" ;;
  pub)     shift; cmd_pub "$@" ;;
  help|-h|--help) usage ;;
  *) usage; exit 1 ;;
esac
