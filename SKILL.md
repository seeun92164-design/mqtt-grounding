# mqtt-grounding

Our own MQTT broker for team **grounding**, separate from the classroom-wide
one. Everyone else on this WiFi is sharing the same network, so all our
topics live under a `grounding/` prefix to keep our traffic out of everyone
else's way (and vice versa).

This wraps the `mosquitto_pub` / `mosquitto_sub` commands behind `skill.sh`,
same interface as the classroom skill, just pointed at our own broker.

## Broker

| | |
|---|---|
| Host | one teammate's laptop runs it — check `Host IP` below |
| Host IP (at setup time) | `192.168.0.35` — **DHCP can change this**, reconfirm with `ipconfig` (Windows) / `ifconfig` (mac/Linux) if `check` fails |
| MQTT (TCP) | `1883` — boards, CLI, desktop clients |
| MQTT over WebSockets | `9001` — browser clients (dashboard) |
| Auth | anonymous, no username or password — fine for a LAN hackathon, don't reuse this config anywhere public |

You must be on the **same WiFi** as the host laptop. This is a private LAN
address, not reachable from outside the room.

### Starting the broker (host laptop only)

```bash
mosquitto -c broker/mosquitto.conf -v
```

Leave this running in a terminal for the rest of the team to connect to.
If teammates can't reach it, check Windows Firewall on the host laptop isn't
blocking inbound TCP 1883 / 9001.

Override the defaults on any teammate's machine with environment variables
if the host's IP changes:

```bash
export MQTT_HOST=<new ip>
export MQTT_PORT=1883
```

## Your device name

Save your board's name once and every command defaults to it:

```bash
./skill.sh name seeun       # save
./skill.sh name             # show what is saved
./skill.sh led on           # no id needed any more
```

It is stored in `~/.mqtt-grounding` (separate from the classroom skill's
`~/.mqtt-classroom`, so registering here doesn't clobber that one).

The name must match `DEVICE_NAME` in the board's `arduino_secrets.h` — that is
what decides the board's topic prefix. Two boards sharing a name will fight
over the same topics, so pick something unique on the team.

## Topics

Each board owns a subtree keyed by its name (`DEVICE_NAME`). The board prints
its name on the serial monitor at boot.

| Topic | Direction | Payload |
|---|---|---|
| `grounding/<id>/led/set` | you → board | `on`, `off`, `toggle` |
| `grounding/<id>/led/state` | board → you | `on`, `off` (retained) |
| `grounding/<id>/sensor/a0` | board → you | `{"raw":2048,"mv":1650}` every 2s |
| `grounding/<id>/status` | board → you | `online`, `offline` (retained, last will) |

`status` and `led/state` are retained, so a fresh subscriber learns the
current state immediately instead of waiting for the next change.

## Usage

```bash
./skill.sh name seeun       # save your board's name (once)
./skill.sh check            # is the broker reachable at all?
./skill.sh devices          # which boards are online
./skill.sh led on           # your board's LED on
./skill.sh led off
./skill.sh led seeun on     # or name another board explicitly
./skill.sh sensor           # stream your board's A0 readings
./skill.sh watch            # every message from your board
```

## Prerequisites

`skill.sh` needs the mosquitto command line clients (the same install gives
you the broker binary too):

- **Windows** — installer from <https://mosquitto.org/download/>, then add
  `C:\Program Files\mosquitto` to `PATH`. Run `skill.sh` from Git Bash.
- **macOS** — `brew install mosquitto`
- **Linux** — `sudo apt install mosquitto-clients`

## Dashboard

`web/dashboard.html` shows every node on the `grounding/` prefix, its live A0
value, and LED buttons. Open the file directly in a browser — no server
needed. Point it at another broker or prefix with query params:

```
web/dashboard.html?host=192.168.0.35&port=9001&prefix=grounding
```

## Connect a new board to the dashboard

This is the flow for "use the skill to connect my Arduino to the dashboard" -
do these steps in order, don't skip the verification ones:

1. **Confirm the broker is reachable**: `./skill.sh check`. If it fails, stop
   here and fix that first (see Troubleshooting) - nothing below will work
   without it.
2. **Pick/confirm a device name**: `./skill.sh name` to see what's saved, or
   `./skill.sh name <newname>` to set one. Must be unique on the team.
3. **Install the PubSubClient library** if not already present:
   `arduino-cli lib install PubSubClient`.
4. **Prepare the sketch**: copy `board/GroundingBoard/` to a working folder
   (or edit it in place), copy `arduino_secrets.h.example` to
   `arduino_secrets.h` inside it, and fill in `DEVICE_NAME` (must match step
   2), `SECRET_SSID`/`SECRET_PASS` (the team WiFi), and `MQTT_HOST` (the
   broker's current IP).
5. **Find the board's port**: `arduino-cli board list`.
6. **Compile and upload**:
   ```bash
   arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32C6 board/GroundingBoard
   arduino-cli upload -p <PORT> --fqbn esp32:esp32:XIAO_ESP32C6 board/GroundingBoard
   ```
7. **Verify from the CLI**: `./skill.sh devices` should list the new device
   name as `online` within a couple seconds of boot.
8. **Verify on the dashboard**: open `web/dashboard.html` in a browser, the
   device's card should appear with a green dot and live A0 readings; the
   ON/OFF buttons should toggle its LED.

If step 7 or 8 doesn't show the board, check the board's serial monitor
(`arduino-cli monitor -p <PORT> -c baudrate=115200`) for `WiFi connected` and
`Connecting to grounding MQTT broker...connected` - whichever of those two
lines is missing tells you whether it's a WiFi problem or a broker problem.

## Board firmware

Boards should connect over WiFi STA to the same LAN as the broker and publish
under the `grounding/<DEVICE_NAME>/...` topics above (PubSubClient works
fine). The ready-to-flash template is in `board/GroundingBoard/`. Key points,
learned the hard way on this team's boards:

- Call `WiFi.setSleep(false)` after `WiFi.begin()` — modem sleep parks
  downlink packets and silently drops the MQTT socket otherwise.
- Set a Last Will (`grounding/<id>/status`, `offline`, retained) on
  `connect()` so a board that loses power/WiFi still shows correctly on the
  dashboard instead of looking permanently "online".
- Publish `status` = `online` (retained) right after a successful connect.

## Browser clients

For a web page, connect over WebSockets rather than TCP:

```js
const client = mqtt.connect('ws://192.168.0.35:9001')
client.subscribe('grounding/+/sensor/a0')
client.publish('grounding/seeun/led/set', 'on')
```

A browser cannot open a raw MQTT TCP socket, so port 1883 will not work from
a web page — that is what the 9001 listener exists for.

## Troubleshooting

| Symptom | Cause |
|---|---|
| `check` fails, connection refused | Broker not running on the host laptop — run `mosquitto -c broker/mosquitto.conf -v` there |
| `check` fails, timeout | Wrong WiFi, host laptop's IP changed (DHCP), or its firewall is blocking the port |
| `devices` prints nothing | No board is powered on, or boards can't reach the broker |
| Board shows `offline` | It lost WiFi or power; its last will fired |
| LED command accepted but nothing happens | Wrong board id — confirm with `./skill.sh devices` |
| `mosquitto_sub not found` | Install the clients; `skill.sh` already looks in the default Windows install path |
| Commands lag seconds or get skipped | Old firmware not calling `WiFi.setSleep(false)` |
| Dashboard shows nothing | Wrong `?host=`/`?prefix=`, or the WebSocket listener (9001) isn't in `broker/mosquitto.conf` on the host |
