#!/bin/bash
# Build and flash the panel over USB through CNC101, the PC it is plugged into.
#
#   tools/flash_cnc101.sh            build, copy, flash app0, reboot
#
# Needs the `cnc101` host in ~/.ssh/config (user cnc, key ~/.ssh/cnc101).
# The panel's CH340 is /dev/serial/by-id/usb-1a86_* on CNC101 (the CP2102 next
# to it is something else -- never flash that one). 460800 baud fails on this
# link; 115200 is reliable (~90 s).
#
# Writes only otadata (boot_app0 -> boot app0) and app0. Bootloader and
# partition table are left alone: the layout has not changed.
set -euo pipefail

HOST="${HOST:-cnc101}"
ESPTOOL='~/.platformio/penv/bin/esptool'
SSH=(ssh -o ClearAllForwardings=yes "$HOST")

cd "$(dirname "$0")/.."
B=.pio/build/crowadv
BOOTAPP=$(find "$HOME/.platformio/packages" -name boot_app0.bin | head -1)

echo "==> building"
pio run -e crowadv

echo "==> copying to $HOST"
scp -q -o ClearAllForwardings=yes "$B/firmware.bin" "$BOOTAPP" "$HOST:/tmp/"

echo "==> flashing"
"${SSH[@]}" "bash -c 'PORT=\$(ls /dev/serial/by-id/usb-1a86_* | head -1) && \
  $ESPTOOL --chip esp32s3 --port \$PORT --baud 115200 write-flash \
  0xe000 /tmp/boot_app0.bin 0x10000 /tmp/firmware.bin'"
echo "==> done"
