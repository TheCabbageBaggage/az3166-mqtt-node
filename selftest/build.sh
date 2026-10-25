#!/bin/bash
# Build the hardware self-test sketch.
#
# The AZ3166 "Sensors" library keeps DevI2C.h in its src/ST_INTERFACES/ folder,
# and arduino-cli only adds libraries/<lib>/src to the include path - not its
# subdirectories - so both have to be passed explicitly.
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
CLI="$HOME/.local/bin/arduino-cli"
GCC="$HOME/az3166-tools/gcc-arm-none-eabi-4.8.3-2014q1/bin/"
PLATFORM="$HOME/Arduino/hardware/AZ3166/stm32f4"
INC="-I$PLATFORM/libraries/Sensors/src/ST_INTERFACES -I$PLATFORM/libraries/Sensors/src/utility -I$DIR/.."

"$CLI" compile --fqbn AZ3166:stm32f4:MXCHIP_AZ3166 \
  --build-property "compiler.path=$GCC" \
  --build-property "compiler.cpp.extra_flags=$INC" \
  --build-property "compiler.c.extra_flags=$INC" \
  --output-dir "$DIR/build" --warnings none "$DIR"

echo "built: $DIR/build/selftest.ino.bin"
