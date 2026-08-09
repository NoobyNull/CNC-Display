#!/bin/bash
# Flash the CrowPanel using two USB adapters.
#
# Why this exists instead of `pio run -t upload`:
#
# The CrowPanel's onboard CH340 has a dead TX line — its TXD never reaches the
# ESP32's GPIO44. Proven by putting the chip in ROM download mode and sending a
# hand-built SLIP sync packet: the CH340 got 0 bytes back, an FTDI wired to the
# same pin got 1344. Everything else about the CH340 is fine, including DTR->GPIO0
# and RTS->EN, which is why it still drives reset perfectly.
#
# So: CH340 for reset control, FTDI for data.
#
#   CH340 (ttyUSB0)  DTR->GPIO0, RTS->EN     reset only
#   FTDI  (ttyUSB1)  TX->GPIO44, RX->GPIO43  data only
#
# If you ever wire the FTDI's DTR/RTS to GPIO0/EN as well, this script becomes
# unnecessary — plain `pio run -e crowpanel -t upload --upload-port /dev/ttyUSB1`
# will work on its own.
set -euo pipefail

RESET_PORT="${RESET_PORT:-/dev/ttyUSB0}"
DATA_PORT="${DATA_PORT:-/dev/ttyUSB1}"

cd "$(dirname "$0")/.."
B=.pio/build/crowpanel
BOOTAPP=$(find "$HOME/.platformio/packages" -name boot_app0.bin | head -1)

echo "==> building"
pio run -e crowpanel

echo "==> entering download mode via $RESET_PORT"
RESET_PORT="$RESET_PORT" python3 - <<'PY'
import os, serial, time, sys
port = os.environ['RESET_PORT']
s = serial.Serial(port, 115200, timeout=0.3)
s.setDTR(False); s.setRTS(False)
s.setDTR(True)                     # GPIO0 low
s.setRTS(True); time.sleep(0.1)    # EN low
s.setRTS(False); time.sleep(0.15)  # release EN, boots with GPIO0 low
s.setDTR(False)                    # release GPIO0
time.sleep(0.4)
ok = b'DOWNLOAD' in s.read(2000)
s.close()
print("    download mode:", "yes" if ok else "NO")
sys.exit(0 if ok else 1)
PY

echo "==> writing over $DATA_PORT"
esptool --chip esp32s3 --port "$DATA_PORT" --baud 460800 \
        --before no-reset --after no-reset write-flash \
        0x0     "$B/bootloader.bin" \
        0x8000  "$B/partitions.bin" \
        0xe000  "$BOOTAPP" \
        0x10000 "$B/firmware.bin"

echo "==> resetting to run"
RESET_PORT="$RESET_PORT" python3 - <<'PY'
import os, serial, time
s = serial.Serial(os.environ['RESET_PORT'], 115200, timeout=0.3)
s.setDTR(False)
s.setRTS(True); time.sleep(0.15); s.setRTS(False)
s.close()
PY
echo "==> done"
