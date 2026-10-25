# AZ3166 → Home Assistant MQTT node

Standalone firmware for the **MXChip / Microsoft AZ3166 IoT DevKit** that turns
the board into a self-contained Home Assistant sensor node: it joins Wi-Fi on
its own, publishes its on-board sensors straight to an MQTT broker, and
registers itself in Home Assistant via MQTT discovery.

No companion PC, no USB-serial bridge, no cloud account. The USB cable only
supplies power — the board works from any 5 V charger.

```
 AZ3166 (STM32F412 + BCM43362)            broker / HA host
 ┌──────────────────────────┐            ┌─────────────────────┐
 │ HTS221  temp / humidity  │            │ mosquitto :1883     │
 │ LPS22HB pressure         │  Wi-Fi     │        │            │
 │ LSM6DSL accel + gyro     │ ─────────► │        ▼            │
 │ LIS3MDL magnetometer     │  MQTT/1883 │ Home Assistant      │
 │ OLED status display      │            │ (MQTT discovery)    │
 └──────────────────────────┘            └─────────────────────┘
```

## What you get in Home Assistant

One device, **AZ3166 DevKit**, with 13 entities, created automatically from the
retained discovery payloads on first connect:

| Entity | Unit | Notes |
|---|---|---|
| Temperature, Humidity | °C, % | HTS221 |
| Pressure | hPa | LPS22HB |
| Acceleration magnitude, X, Y, Z | g | LSM6DSL, magnitude derived |
| Gyroscope X, Y, Z | °/s | diagnostic |
| Magnetometer X, Y, Z | raw counts | diagnostic |

## MQTT interface

| Topic | Retained | Payload |
|---|---|---|
| `az3166/state` | no | `{"temperature":23.90,"humidity":49.00,"pressure":996.06,"accel_total":0.999,...,"mag_z":82}` every 10 s |
| `az3166/availability` | yes | `online` / `offline` — `offline` is the MQTT last-will, so HA marks the entities unavailable when the board loses power |
| `homeassistant/sensor/az3166_<field>/config` | yes | discovery payloads, republished on every (re)connect |

The base topic is set by `MQTT_BASE`; the discovery prefix is `homeassistant`.

## Behaviour

* 10 s publish interval, reconnect logic for both Wi-Fi and the broker.
* OLED shows four lines: title, IP address, `HA: online|offline`, temperature + humidity.
* Wi-Fi LED is lit while associated, the Azure LED once the broker connection is up.
* Single-threaded `loop()`, static payload buffers — no RTOS tasks, no per-publish allocation.

## Hardware

MXChip AZ3166 IoT DevKit (discontinued, but plentiful second-hand):
STM32F412ZG Cortex-M4F, Cypress BCM43362 Wi-Fi (2.4 GHz, WPA2 — **no 5 GHz, no
WPA3-only APs**), 128×64 OLED, two user buttons, RGB LED, stereo mic +
headphone out, IR emitter, HTS221 / LPS22HB / LSM6DSL / LIS3MDL sensors, and an
ST-Link/V2.1 on board for flashing.

## Build

The official Arduino board index for this board
(`azureboard.blob.core.windows.net`) is **dead** — every platform and tool URL
returns 404 — so the platform has to be assembled by hand. The steps below
assume Linux; macOS works the same with the matching toolchain build.

### 1. Toolchain

Use the *old* Arduino `gcc-arm-none-eabi 4.8.3`. It is the last one that builds
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

Get the devkit SDK (a maintained fork of Microsoft's, since the original repo is
archived) and register it as a *manual* platform in the sketchbook:

```bash
git clone --depth 1 https://github.com/fmuntean/az3166-devkit-sdk.git ~/az3166-sdk
mkdir -p ~/Arduino/hardware/AZ3166
cp -r ~/az3166-sdk/AZ3166/src ~/Arduino/hardware/AZ3166/stm32f4

# one-line fix: this header relies on transitive <stdint.h> includes
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

`./flash.sh` does build + flash in one go (it calls `pkexec` for openocd).

## Flash

The application lives at **0x0800C000**: the ST-Link bootloader owns the first
48 KiB. Drag-and-drop onto the `AZ3166` mass-storage volume writes to
`0x08000000` and destroys that bootloader — always use openocd with the offset:

```bash
sudo openocd -f interface/stlink.cfg -c "transport select hla_swd" \
  -f target/stm32f4x.cfg \
  -c "program build/az3166-mqtt-node.ino.bin verify reset 0x0800C000; shutdown"
```

### Keep a way back

Dump the factory image before the first flash:

```bash
sudo openocd -f interface/stlink.cfg -c "transport select hla_swd" -f target/stm32f4x.cfg \
  -c "init; reset halt; dump_image stock-firmware.bin 0x08000000 0x100000; shutdown"
```

Restore it with `program stock-firmware.bin verify reset 0x08000000`.

## Gotchas this firmware already works around

These cost real debugging time, so they are worth knowing before you fork it:

* **newlib mismatch.** The prebuilt `libdevkit-sdk-core-lib.a` / `libwlan.a`
  were built against newlib 2.4, while the 4.8.3 toolchain ships newlib 2.1.
  The sketch supplies `__locale_ctype_ptr` (returning `__ctype_ptr__`) and
  `itoa`/`utoa` in **both** plain C and the core's C++ mangled form
  (`_Z4itoaiPci`, `_Z4utoajPci`) through `__asm__` labels.
* **`AZ3166WiFiClient` is not Arduino's WiFiClient.** `available()` returns
  `connected()` — a bool, not a byte count — and `connect()` puts the socket in
  non-blocking mode, so `write()` can be partial and `read()` returns 0 until
  data arrives. Both the CONNACK read and every packet write need retry loops;
  without them the board connects and drops every ~5 s (watch for that pattern
  in the broker log if you touch the networking code).
* **Sensor units.** `getDevKitAcceleratorValue` returns mg (÷1000 → g),
  `getDevKitGyroscopeValue` returns mdps (×0.004375 → °/s), and the
  magnetometer returns raw counts. The published JSON is already normalised.
* **2.4 GHz / WPA2 only.** The BCM43362 will not join 5 GHz or WPA3-only SSIDs.
  The broker must be reachable by IP — there is no DNS lookup or TLS in this
  firmware.

## Ideas that fit the same firmware

The board also has two buttons, an RGB LED, audio out, an IR emitter and a
microphone, none of which the current firmware touches: feed/nap timers on the
buttons, a white-noise-machine watchdog using the mic, a dim night light on the
RGB LED, or an IR blaster for the TV. Each of those is "add a subscriber plus
one handler" away from what is here.

## License

MIT — see [LICENSE](LICENSE).

The platform sources (mbed-os, WICED/EMW1062 driver, Azure IoT C SDK) are not
part of this repository; they come from the devkit SDK under their own licences.
The MQTT client here is hand-written (MQTT 3.1.1, QoS 0 only) and deliberately
small.
