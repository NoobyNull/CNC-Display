# Handoff — CNC Console, 2026-08-08

State at the end of the first build session. Everything below is committed and
pushed to `https://git.yax.family/matthew/CNC-Disply` (branch `main`).

---

## One-line summary

The firmware is written, builds clean and passes all its tests. **It has not been
flashed since 20:00** because the board cannot be put into download mode — a
wiring fault, not a software problem. Fix one solder joint and it flashes in
15 seconds.

---

## Hardware, as established by measurement

Elecrow CrowPanel Advance 7.0", ESP32-S3-WROOM-1, 8 MB PSRAM, 800×480 RGB, GT911 touch.

### GPIO map

| Pins | Use |
|---|---|
| 0,1,2,3,4,5,6,7,8,9,14,15,16,21,39,40,41,45,46,47,48 | RGB panel + backlight |
| 19 (SDA), 20 (SCL) | I2C: GT911 `0x5D`, PCA9557 `0x18`, **ICM-20948 `0x68/0x69`** |
| 10, 11, 12, 13 | microSD (CS/MOSI/CLK/MISO) — panel's own card |
| 38 | power rail / spare |
| **43, 44** | UART0 pads — **console OR FluidNC link**, see link mode |
| 26–37 | octal PSRAM + flash, unusable |
| 17, 18, 42 | free, but **not broken out on this board** |

Only **19, 20, 38** and **43/44** are physically accessible on connectors. That
constraint drove most of the design decisions.

### Current wiring

FTDI FT232R (`usb-FTDI_FT232R_USB_UART_BG012EQJ`) soldered to the module:

| FTDI | Module pin | Status |
|---|---|---|
| RX | 37 (TXD0 / GPIO43) | ✅ working |
| TX | 36 (RXD0 / GPIO44) | ✅ working (intermittent earlier — watch it) |
| GND | 1 or 40 | ✅ |
| RTS | 3 (EN) | ✅ resets the board |
| DTR | 27 (IO0) | ❌ **THE BLOCKER — see below** |

---

## THE BLOCKER

`pio run -t upload` fails at "Failed to connect … No serial data received".

Reset works (RTS pulses EN, board reboots), but the chip **never enters download
mode** — it reports `boot:0x8 (SPI_FAST_FLASH_BOOT)` every time instead of
`boot:0x0 (DOWNLOAD)`.

**Suspected cause: EN and IO0 are shorted together.** The user reported "en is
connected to io0". If those two nets are common, download mode is physically
impossible: the ROM samples IO0 at the moment EN rises, and if they are tied,
IO0 rises with EN and always reads high.

**To fix:** with the FTDI unplugged, check continuity between module pin 3 (EN)
and pin 27 (IO0). It must read **open**. They need separate wires:

```
FTDI RTS ──→ pin 3   (EN)      reset only
FTDI DTR ──→ pin 27  (IO0)     boot select only, no link to EN
```

Also confirm pin 27 really is IO0 — it is the bottom-right corner pin, the one
the numbering wraps to after the bottom row ends at 26. One position up is IO35
(pin 28), which does nothing.

**Verify the fix** — this should print `DOWNLOAD`:

```bash
python3 - <<'PY'
import serial, time
P='/dev/serial/by-id/usb-FTDI_FT232R_USB_UART_BG012EQJ-if00-port0'
s = serial.Serial(P, 115200, timeout=0.5)
s.setDTR(False); s.setRTS(False); time.sleep(0.2)
s.setDTR(True)                      # IO0 low
s.setRTS(True); time.sleep(0.12)    # EN low
s.setRTS(False); time.sleep(0.25)   # release EN, boots with IO0 low
s.setDTR(False)
time.sleep(1.0)
d = s.read(4000)
print("DOWNLOAD" if b'DOWNLOAD' in d else "still normal boot")
s.close()
PY
```

**Then flash:**

```bash
pio run -e crowpanel -t upload \
  --upload-port /dev/serial/by-id/usb-FTDI_FT232R_USB_UART_BG012EQJ-if00-port0
```

### The CH340 (onboard USB-C) — do not use for data

Its **TX line is dead**. Proven twice by putting the chip in ROM download mode
and sending a hand-built SLIP sync packet: CH340 got 0 bytes back, the FTDI on
the same pin got 1344. Its DTR/RTS still work, so it can reset.

**Do not connect it and the FTDI at the same time.** Both TX lines drive GPIO44;
two push-pull outputs on one wire is a bus fight, and the CH340's dead TX may be
stuck driving. Flashing was unreliable all evening until the CH340 was unplugged.

`tools/flash.sh` automates the two-adapter workaround (CH340 reset + FTDI data)
and can be deleted once DTR→IO0 works.

---

## What is on the board right now

The **20:00 build**, which is older than HEAD. It has:

- FluidNC UART on GPIO 11/13 (since moved to 43/44)
- Console on 43/44 at 115200 — prints `[link] state=…` every second and a
  `[scan]` block at boot
- No transports UI, no SD, no settings editor, no IMU

If you see `[scan] GPIO 10 edges=0` in the log, that is the old firmware.

## What is committed but NOT flashed

Six features, 51/51 host tests passing, 47.4% flash, 28.6% RAM:

1. **Pendant on GPIO 43/44** + **UPDATE / PENDANT link mode** (Setup page).
   UPDATE is the default: console owns 43/44, wired transport off, board always
   flashable. PENDANT hands those pins to FluidNC and parks the console on
   GPIO 42 (unconnected).
2. **Runtime transport selection** — Wired UART and WiFi toggles in Setup, both
   NVS-backed. WiFi is Telnet discovered by mDNS (`_telnet._tcp`), so no IP is
   needed. Hunts 1 M ↔ 115200 on the wire, falls back to WiFi.
3. **Panel SD as G-code source** — browse the panel's card, push files to the
   controller with `$XR`, run with `$SD/Run=`. Chosen over line-by-line
   streaming so the pendant can be unplugged mid-cut without stopping the job.
4. **FluidNC settings editor** on the Diag page — `$S` / `$SC`, edit a value,
   writes `$name=value`.
5. **ICM-20948 vibration guard** — monitor / soft / hard modes, defaults to
   monitor.
6. **Baud hunting** in the link watchdog.

---

## Things a future session should not have to rediscover

- **Toolchain**: must be the pioarduino platform, not `espressif32@6.9.0`.
  The older one cannot add this board's OPI PSRAM to the heap — you get
  "External RAM could not be added to heap!" and an abort loop at `cpu_start`,
  with only 434 KB of PSRAM visible. FluidDial pins 6.9.0 but targets boards
  without OPI PSRAM.
- **`Wire.begin(19, 20)` must run before `display_init()`** — it drives the
  PCA9557 immediately, and without the bus up every I2C call fails with
  "NULL TX buffer pointer".
- **`touch.cpp` is adapted, not copied.** The hotkey project's version delayed
  LVGL's press report by 120 ms to tell taps from swipes and suppressed input
  after 30 px of movement. A jog key must register on contact.
- **Never chain `send_line()` calls at boot.** `fnc_send_line()` spins in
  `while (_ackwait)` until the previous line is acked or its 2000 ms timeout
  expires; with no controller attached that freezes the panel before the first
  frame is painted.
- **`handle_other_line()` must sit above the early returns** in `handle_other()`.
  The file-list reply is the `'{'` case that returns into `handle_json()`.
- **`myAxes` is the WORK position** — `show_dro()` has already subtracted WCO.
  Machine position is `myAxes + myWco`.
- **CNC-TW's jerk threshold is unsafe as written** (see below).

## A defect found in CNC-TW worth fixing there too

`/home/matthew/Desktop/CNC-TW` escalates to e-stop on **jerk alone**. Jerk is a
per-sample delta over the sample interval, so any vibration large enough to
reach the 0.5 g RMS hold threshold also produces jerk far above 50 g/s — at its
200 Hz sample rate a 0.5 g swing is already 100 g/s. The feed-hold tier is
therefore unreachable and every rough cut becomes an e-stop.

This port gates jerk on magnitude: it only counts when the deviation is also at
least half the peak threshold — an impact is fast **and** large.
`test_sustained_vibration_does_not_escalate_to_estop` pins the behaviour.

---

## Reference

| | |
|---|---|
| Spec | `docs/superpowers/specs/2026-08-08-cnc-console-design.md` |
| Plan + execution notes | `docs/superpowers/plans/2026-08-08-cnc-console.md` |
| Layout study | https://claude.ai/code/artifact/29f1b135-2fd7-40dc-b26b-bc20692e0f22 |
| Build | `pio run -e crowpanel` |
| Tests | `pio test -e native` (51 cases) |
| Repo | `https://git.yax.family/matthew/CNC-Disply` |

### Topology

```
CrowPanel display ──UART 1M──► Airdale (passthrough) ──► Corgi (FluidNC)
```

The Airdale is a passthrough, not the controller. FluidNC runs on the Corgi, and
its SD card is unreachable inside a sealed box — which is why the panel's own
card is the G-code source.

### Not yet verified on hardware

Nothing past the 20:00 build has run on the panel. Once flashed, work through
the acceptance checklist at the end of the plan document. The one that matters
most: **press a jog key, slide your finger off without lifting, confirm the axis
stops.** That is the `LV_EVENT_PRESS_LOST` path.
