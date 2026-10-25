# AZ3166 → Home Assistant

**A MXChip AZ3166 IoT DevKit as a standalone MQTT sensor node for Home Assistant.** No Azure, no cloud, no vendor account, no PC-side bridge — the board joins Wi-Fi itself, publishes its on-board sensors, and appears in Home Assistant as one device with 13 entities via MQTT discovery. The USB cable only supplies power.

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![MCU](https://img.shields.io/badge/MCU-STM32F412-blue.svg)](#hardware)
[![Home Assistant](https://img.shields.io/badge/Home%20Assistant-MQTT%20discovery-41BDF5.svg)](https://www.home-assistant.io/integrations/mqtt/)
[![Flash](https://img.shields.io/badge/flash%20%2B%20verify-OK-brightgreen.svg)](#measured-on-hardware)
[![Toolchain](https://img.shields.io/badge/arduino--cli%20%2B%20gcc%204.8.3-working-informational.svg)](#build)
[![PRs welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg)](CONTRIBUTING.md)

```
 AZ3166 (STM32F412 + BCM43362)              mosquitto + Home Assistant
 ┌────────────────────────────┐            ┌──────────────────────────┐
 │ HTS221   temperature/hum.  │            │  :1883  ──►  MQTT        │
 │ LPS22HB  pressure          │   Wi-Fi    │            discovery     │
 │ LSM6DSL  accel + gyro      │ ─────────► │                          │
 │ LIS3MDL  magnetometer      │  MQTT/1883 │  13 entities, 1 device   │
 │ 128×64 OLED status display │            │  availability via LWT    │
 └────────────────────────────┘            └──────────────────────────┘
```

## Contents

- [Why this exists](#why-this-exists)
- [Features](#features)
- [Quick start](#quick-start)
- [What you get in Home Assistant](#what-you-get-in-home-assistant)
- [MQTT interface](#mqtt-interface)
- [Measured on hardware](#measured-on-hardware)
- [How it works](#how-it-works)
- [Build](#build)
- [Flash](#flash)
- [Troubleshooting](#troubleshooting)
- [Use cases](#use-cases)
- [Self-test sketch](#self-test-sketch)
- [When not to use this board](#when-not-to-use-this-board)
- [License](#license)

## Why this exists

The AZ3166 is a lovely devkit with a terrible 2026 story: the board is
discontinued, the official Arduino board index
(`azureboard.blob.core.windows.net`) returns **404** for every platform and
toolchain URL, Microsoft's SDK repository is archived, and the sample code all
points at Azure IoT Hub.

This repository is the working path: a hand-assembled toolchain, the four
link-time fixes it takes to build at all, a ~380-line firmware with its own
small MQTT client, and an honest list of what the hardware can and cannot do.
Everything here was flashed to a real board and verified against a real broker
— no datasheet copy-paste, no "should work".

## Features

- **Standalone** — Wi-Fi credentials and broker address live in the firmware; nothing runs on a PC.
- **Auto-discovery** — one device, 13 entities, created in Home Assistant on first connect. No YAML.
- **Honest availability** — the MQTT last-will marks the entities `unavailable` when the board loses power, instead of showing stale values forever.
- **Reconnects by itself** — separate retry loops for Wi-Fi and the broker; survives AP reboots and broker restarts.
- **Glanceable** — the OLED shows IP, HA link state, temperature and humidity; the board is its own status display.
- **Dependency-free** — hand-written MQTT 3.1.1 client (QoS 0), static buffers, no `PubSubClient`, no JSON library, no TLS.
- **Portable power** — runs from any 5 V USB supply.

## Quick start

```bash
git clone https://github.com/TheCabbageBaggage/az3166-mqtt-node.git && cd az3166-mqtt-node
cp net_config.h.example net_config.h && chmod 600 net_config.h   # SSID, broker, creds
./flash.sh                                                       # build + flash, needs the toolchain from Build
```

The toolchain is not a one-liner (the upstream index is dead) — do
[Build](#build) steps 1–3 once, then the two commands above.

## What you get in Home Assistant

| Entity | Unit | Source | Category |
|---|---|---|---|
| Temperature | °C | HTS221 | primary |
| Humidity | % | HTS221 | primary |
| Pressure | hPa | LPS22HB | primary |
| Acceleration magnitude | g | LSM6DSL, derived | primary |
| Acceleration X / Y / Z | g | LSM6DSL | primary |
| Gyroscope X / Y / Z | °/s | LSM6DSL | diagnostic |
| Magnetometer X / Y / Z | raw | LIS3MDL | diagnostic |

All 13 land under one device (`AZ3166 DevKit`) with `state_class: measurement`,
so long-term statistics and the history graph work without extra configuration.

## MQTT interface

| Topic | Retained | Payload |
|---|---|---|
| `az3166/state` | no | `{"temperature":23.90,"humidity":49.00,"pressure":996.06,"accel_total":0.999,"accel_x":0.10,"accel_y":-0.87,"accel_z":-0.48,"gyro_x":0.84,"gyro_y":-1.96,"gyro_z":0.28,"mag_x":192,"mag_y":170,"mag_z":82}` |
| `az3166/availability` | **yes** | `online` / `offline` — `offline` is the last-will |
| `homeassistant/sensor/az3166_<field>/config` | **yes** | discovery payload, re-published on every connect |

Publish interval is 10 s (`STATE_INTERVAL_MS`). Base topic and credentials come
from `net_config.h`; the discovery prefix is `homeassistant`.

## Measured on hardware

Read off this board with openocd and the running firmware, not from datasheets:

| Item | Value |
|---|---|
| MCU | STM32F412, openocd device id `0x30006441`, 1024 KiB flash |
| Application base | `0x0800C000` (the ST-Link bootloader owns the first 48 KiB) |
| Firmware size | 391 100 B flash (37 %), 47 608 B RAM (18 %) |
| Self-test sketch | 231 224 B flash (22 %) |
| Pressure noise | ≈ ±0.1 hPa — good enough to see weather fronts |
| Accelerometer noise | ≈ ±0.01 g — vibration detection is reliable |
| Gyroscope | uncalibrated, bias in the 1000 mdps range |
| Radio | 2.4 GHz / WPA2 only — no 5 GHz, no WPA3-only APs |
| Updates | ST-Link only — **no OTA** |

## How it works

1. `initIoTDevKit()` brings up I2C, the four sensors, the OLED and the LEDs.
2. `WiFi.begin(ssid, pass)` associates; the Wi-Fi LED lights on success.
3. Four packets open the MQTT session: `CONNECT` (with will), `CONNACK`, then 13
   retained discovery publishes, then `availability = online`.
4. `loop()` publishes a JSON sample every 10 s and drops back into the reconnect
   path on any error. No subscriptions, no RTOS tasks, no per-publish allocation.

## Build

The platform has to be assembled by hand because the official index is dead.
Linux below; macOS works the same with the matching toolchain build.

### 1. Toolchain

Use the *old* Arduino `gcc-arm-none-eabi 4.8.3` — it is the last one that builds
this 2016-era mbed-os/WICED tree (a modern gcc fails on it), and Arduino still
serves it:

```bash
mkdir -p ~/az3166-tools
curl -LO http://downloads.arduino.cc/gcc-arm-none-eabi-4.8.3-2014q1-linux64.tar.gz
tar xzf gcc-arm-none-eabi-4.8.3-2014q1-linux64.tar.gz -C ~/az3166-tools
```

### 2. arduino-cli

```bash
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | BINDIR=~/.local/bin sh
```

### 3. Platform

The devkit SDK (a maintained fork of Microsoft's, since the original repo is
archived) registered as a *manual* platform in the sketchbook:

```bash
git clone --depth 1 https://github.com/fmuntean/az3166-devkit-sdk.git ~/az3166-sdk
mkdir -p ~/Arduino/hardware/AZ3166
cp -r ~/az3166-sdk/AZ3166/src ~/Arduino/hardware/AZ3166/stm32f4

# one-line fix: this header relies on a transitive <stdint.h> include
sed -i 's/#define __VERSION_H__/#define __VERSION_H__\n\n#include <stdint.h>/' \
  ~/Arduino/hardware/AZ3166/stm32f4/cores/arduino/system/SystemVersion.h

arduino-cli board listall | grep -i az3166   # -> MXCHIP AZ3166  AZ3166:stm32f4:MXCHIP_AZ3166
```

### 4. Configure and compile

```bash
cp net_config.h.example net_config.h && chmod 600 net_config.h
$EDITOR net_config.h          # 2.4 GHz SSID/PSK, broker IP, credentials

arduino-cli compile --fqbn AZ3166:stm32f4:MXCHIP_AZ3166 \
  --build-property "compiler.path=$HOME/az3166-tools/gcc-arm-none-eabi-4.8.3-2014q1/bin/" \
  --output-dir build .
```

## Flash

The application lives at **0x0800C000**: the ST-Link bootloader owns the first
48 KiB. Drag-and-drop onto the `AZ3166` mass-storage volume writes to
`0x08000000` and destroys that bootloader — always use openocd with the offset:

```bash
sudo openocd -f interface/stlink.cfg -c "transport select hla_swd" \
  -f target/stm32f4x.cfg \
  -c "program build/az3166-mqtt-node.ino.bin verify reset 0x0800C000; shutdown"
```

`./flash.sh` wraps the compile and this command (`pkexec` instead of `sudo`).

### Keep a way back

Dump the factory image before the first flash, restore it the same way:

```bash
# backup (1 MiB, whole flash)
sudo openocd -f interface/stlink.cfg -c "transport select hla_swd" -f target/stm32f4x.cfg \
  -c "init; reset halt; dump_image stock-firmware.bin 0x08000000 0x100000; shutdown"

# restore
sudo openocd -f interface/stlink.cfg -c "transport select hla_swd" -f target/stm32f4x.cfg \
  -c "program stock-firmware.bin verify reset 0x08000000; shutdown"
```

## Troubleshooting

**The board connects to the broker and drops every ~5 s.**
`AZ3166WiFiClient` is not Arduino's `WiFiClient`: `available()` returns
`connected()` — a bool, not a byte count — and `connect()` switches the socket
to **non-blocking**, so `write()` can be partial and `read()` returns 0 until
data arrives. Both the CONNACK read and every packet write need retry loops.
This is the single most time-consuming trap in this port.

**`undefined reference to __locale_ctype_ptr` / `utoa`.**
The prebuilt `libdevkit-sdk-core-lib.a` and `libwlan.a` were built against
newlib 2.4, gcc 4.8.3 ships newlib 2.1. `newlib_shim.h` supplies
`__locale_ctype_ptr`, `itoa`/`utoa` in plain C, and the core's C++ mangled forms
(`_Z4itoaiPci`, `_Z4utoajPci`) via `__asm__` labels. Include it once per sketch.

**`fatal error: DevI2C.h: No such file or directory`.**
arduino-cli adds only `libraries/<lib>/src` to the include path, not its
subfolders, and `DevI2C.h` lives in `src/ST_INTERFACES/`. Pass it explicitly —
`selftest/build.sh` has the exact `--build-property` incantation.

**Sensor values look off by a factor of 1000.**
`getDevKitAcceleratorValue` returns **mg** (÷1000 → g),
`getDevKitGyroscopeValue` returns **mdps** (×0.004375 → °/s), and the
magnetometer returns raw counts. The published JSON is already normalised.

**Nothing reaches the broker.**
The firmware has no DNS and no TLS: `MQTT_HOST` must be an IP, port 1883, and
the AP must be 2.4 GHz WPA2. Watch `docker logs mosquitto` — if the client never
appears there, the board never got on the network.

## Use cases

What this board is actually good for — ranked, with effort tags and measured
numbers — is in **[USE_CASES.md](USE_CASES.md)**: room climate and mould early
warning, washing-machine cycles, pressure trends, nursery monitoring, an OLED
info panel, mains-current sniffing, gesture control, plus the ideas that sound
good but are not worth the cable.

## Self-test sketch

`selftest/` proves the hardware before you trust the node: Wi-Fi scan, sensor
init codes, live values, LEDs and OLED. No credentials needed.

```bash
./selftest/build.sh     # then flash selftest/build/selftest.ino.bin at 0x0800C000
```

## When not to use this board

- **A fleet across several rooms** — no OTA, and one board is one node; an ESP32 with ESPHome is the sane backbone.
- **Anything 5 GHz, WPA3, BLE, Thread or Zigbee** — the radio firmware is from 2014; no update will add it.
- **Reference-grade measurements** — HTS221 is ±0.5 K / ±3.5 % RH and the board self-heats a couple of Kelvin.
- **Door and window contacts** — a €5 Zigbee sensor is cheaper, better, and needs no USB cable.
- **A baby monitor or any safety device** — this is a hobby sensor node, not a certified monitor.

## License

MIT — see [LICENSE](LICENSE).

The platform sources (mbed-os, WICED/EMW1062 driver, Azure IoT C SDK) are *not*
in this repository; they come from the
[devkit SDK fork](https://github.com/fmuntean/az3166-devkit-sdk) under their own
licences. Thanks to Microsoft and MXChip for the original SDK, and to that fork
for keeping it buildable.
