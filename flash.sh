#!/bin/bash
# Build the AZ3166 firmware and flash it over ST-Link (needs root: pkexec).
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
CLI="$HOME/.local/bin/arduino-cli"
GCC="$HOME/az3166-tools/gcc-arm-none-eabi-4.8.3-2014q1/bin/"

echo "== build =="
"$CLI" compile --fqbn AZ3166:stm32f4:MXCHIP_AZ3166 \
  --build-property "compiler.path=$GCC" \
  --output-dir "$DIR/build" --warnings none "$DIR"

BIN="$DIR/build/az3166-mqtt-node.ino.bin"
ls -l "$BIN"

echo "== flash @0x0800C000 =="
pkexec openocd -f interface/stlink.cfg -c "transport select hla_swd" \
  -f target/stm32f4x.cfg \
  -c "program $BIN verify reset 0x0800C000; shutdown"
