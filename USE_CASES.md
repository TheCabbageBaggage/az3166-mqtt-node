# Use cases

What this board is genuinely good for, ranked by payoff per effort, with the
measured numbers that decide whether a case works at all. Effort tags:

| Tag | Meaning |
|---|---|
| `ready` | works with the firmware in this repo — Home Assistant configuration only |
| `code` | needs a firmware addition in this repo (no extra hardware) |
| `hardware` | needs a part beyond the devkit and a 5 V USB supply |

Intervals below assume the shipped 10 s publish rate (`STATE_INTERVAL_MS`).

## Ranked

| # | Use case | Effort | Why it works |
|---|---|---|---|
| 1 | [Room climate + mould early warning](#1-room-climate--mould-early-warning) | `ready` | HTS221 ±0.5 K / ±3.5 % RH is enough for trends and alarms |
| 2 | [Washing machine / dryer finished](#2-washing-machine--dryer-finished) | `ready` | 0.45 g trigger against ±0.01 g noise — no false positives |
| 3 | [Nursery room monitoring](#3-nursery-room-monitoring) | `ready` | Overheating risk is a temperature question; one offset fixes self-heating |
| 4 | [Pressure trend as weather precursor](#4-pressure-trend-as-weather-precursor) | `ready` | ±0.1 hPa noise against 5–20 hPa real weather swings |
| 5 | [Fridge / freezer failure alarm](#5-fridge--freezer-failure-alarm) | `ready` | Temperature trend + door-open blip, no sensor in the food |
| 6 | [Tamper / movement alarm](#6-tamper--movement-alarm) | `ready` | Same threshold math as the washing machine |
| 7 | [Config over MQTT](#7-config-over-mqtt) | `code` | No OTA: today every threshold change means a USB cable |
| 8 | [OLED info panel](#8-oled-info-panel) | `code` | The one thing no off-the-shelf sensor does |
| 9 | [Button remote + night light](#9-button-remote--night-light) | `code` | Two buttons and an RGB LED already on the board |
| 10 | [White-noise / appliance watchdog (mic)](#10-white-noise--appliance-watchdog-mic) | `code` | Replaces a dedicated acoustic sensor |
| 11 | [Mains-current sniffer](#11-mains-current-sniffer) | `code` | Magnetometer reads the 50 Hz field of a cable |
| 12 | [Gesture remote](#12-gesture-remote) | `code` | Accelerometer gestures instead of a phone |
| 13 | [IR blaster](#13-ir-blaster) | `code` | `IRDATransmit` ships with the core, NEC devices only |

## Works today — Home Assistant configuration only

### 1. Room climate + mould early warning

Temperature, humidity and pressure already publish every 10 s. Home Assistant
computes dew point and the margin to the coldest wall surface and notifies
below ~3 K. Period buildings with cold outer walls grow mould long before it is
visible.

Caveats: the board self-heats a couple of Kelvin — compare against a decent
thermometer once and apply a fixed offset — and do not place it at a window or
above a radiator. HTS221 is a trend sensor, not a reference instrument.

### 2. Washing machine / dryer finished

Tape the board to the machine, power it from a USB supply, and treat
`accel_total > 0.45 g` for two minutes as "running". Measured noise is
±0.01 g, so false triggers are effectively excluded. Notification automation
fires when vibration stops after having been high. No mains measurement, no
modification of the appliance.

### 3. Nursery room monitoring

Same data as case 1, different automation: alert above 21–22 °C, below 16 °C,
and humidity under 40 % in winter. Mount on a shelf — never on or in the cot —
and route the USB cable out of reach. Optionally pair with the button timer
(case 9) and the white-noise watchdog (case 10) for a nursery dashboard on the
OLED.

**This is not a baby monitor and must not be used as a safety device.** No
audio streaming, no breathing or apnoea detection, no medical claims.

### 4. Pressure trend as weather precursor

"Pressure falling fast" over a few hours is a useful short-range hint and costs
one template sensor. Noise is ±0.1 hPa against real changes of 5–20 hPa, so the
trend is trustworthy — the forecast it implies is not.

### 5. Fridge / freezer failure alarm

Put the board (USB supply, cable through the door seal) in the fridge: rising
temperature plus the pressure blip of the door opening gives "door left open"
and "compressor dead" alarms before the food is lost. Wi-Fi reaches through a
normal fridge wall at this distance; verify before trusting it.

### 6. Tamper / movement alarm

Same threshold logic as case 2, inverted: any movement above ~0.05 g while
armed publishes immediately. Bike shed, toolbox, letterbox. Runs from a power
bank, so it works where there is no socket.

## Firmware additions — an evening each

### 7. Config over MQTT

Prerequisite for everything below before the node disappears into a shelf:
thresholds, publish interval and target topics should come from a retained
config topic instead of compile-time constants. Without this, tuning anything
means carrying the board back to the laptop and re-flashing.

### 8. OLED info panel

Next departures, waste pickup dates, outside temperature, printer state, time
since the last feed. Needs MQTT **subscribe** (the firmware currently only
publishes) plus a publisher on the Home Assistant side. This is the case no
off-the-shelf sensor covers, and the reason the 128×64 display is worth having.

### 9. Button remote + night light

Two buttons as scene triggers ("everything off", "dim the lights") and the RGB
LED as a dim night light or status lamp: red = door open, green = all quiet.
Note the catch found the hard way: `USER_BUTTON_A/B` are MiCO macros used by the
core but not exposed to sketches, so button handling needs the BSP header or
direct `PinName` access rather than the usual `digitalRead`.

### 10. White-noise / appliance watchdog (mic)

A microphone RMS level with hysteresis answers "is the white-noise machine
still running", "did the steriliser finish", "is the doorbell ringing" — one
board replacing several dedicated acoustic sensors. The NAU88C10 codec is
reachable through the `AudioV2` library; level detection only, no audio
streaming, which keeps it privacy-friendly.

### 11. Mains-current sniffer

Tape the board next to a mains cable: the magnetometer sees the 50 Hz field
strength, which tracks the appliance's current draw. Non-invasive "is the oven /
iron / washing machine actually on" detection with no electrical work. Needs
fast magnetometer sampling — the shipped 10 s tick aliases past 50 Hz — and
calibration per cable, so treat it as a fun experiment, not a meter.

### 12. Gesture remote

Wave or flip the board, `accel_total` and gyro signature become an MQTT event:
lights off, TV pause, "I'm going to bed". Fun, demo-friendly, and needs a
gesture filter so a bumped desk does not switch the lights.

### 13. IR blaster

`IRDATransmit` ships with the core; good for NEC-style devices (TV, fan, older
AC). Complex air-conditioner protocols (Daikin, Mitsubishi) are painful, and it
is send-only — receiving IR needs a TSOP demodulator on a free pin.

## Not recommended

- **Window handle / tilt detection.** The accelerometer could do it (±0.01 g ≈
  0.6°), but a USB cable to a window handle is not practical. A €5 Zigbee
  contact sensor is cheaper and better.
- **Fleet across rooms.** No OTA, one board is one node. ESP32 + ESPHome is the
  sane backbone; keep this board as the interesting single node.
- **Calibration reference.** HTS221 is too coarse.
- **5 GHz, WPA3, BLE, Thread/Zigbee.** Not present, and no firmware update will
  add it — the radio firmware dates from 2014.
- **Safety equipment.** No certified monitoring, no alarms that matter.

## Hardware notes

- Free pins through the Arduino D-mapping: `PA_4`, `PA_5`, `PB_0…PB_15`, `PC_6`,
  `PC_7` — enough for a reed contact or a TSOP IR receiver.
- Power: any 5 V USB supply; no battery connector, no deep sleep in this
  firmware (the board idles at ~40 mA).
- Sensors on I2C: HTS221, LPS22HB, LSM6DSL, LIS3MDL, all initialised by
  `initIoTDevKit()`.
