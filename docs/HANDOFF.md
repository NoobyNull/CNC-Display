# Handoff — CNC Console, 2026-08-10

The firmware moved from the Elecrow CrowPanel Advance 7.0" to the **Waveshare
ESP32-S3-Touch-LCD-7B**. The old board was abandoned because it could not be put
into download mode: its CH340's TX line was dead and EN was shorted to IO0. None
of that survives here — the 7B flashes over native USB.

Everything above the driver layer is unchanged. The port touched the panel, the
touch reset path, the SD card, the console, and the layout constants.

---

## Status

Builds clean (`23.8%` flash of a 6 MB app partition, `28.6%` RAM), 51/51 host
tests pass. **Nothing has run on the new hardware** — it arrives 2026-08-11.
Work through the acceptance checklist at the end of the plan document once it
does; the one that matters most is still: press a jog key, slide your finger off
without lifting, confirm the axis stops (`LV_EVENT_PRESS_LOST`).

---

## Hardware

ESP32-S3-WROOM-1-**N16R8** (16 MB flash, 8 MB octal PSRAM), 1024×600 RGB565
panel behind an ST7262, GT911 touch, CH32V003 I2C io-extender, CH343P USB-UART,
native USB, RS485 (SP3485) and CAN (TJA1051).

### GPIO map

| Pins | Use |
|---|---|
| 0,1,2,3,5,7,10,14,17,18,21,38,39,40,41,42,45,46,47,48 | RGB panel |
| 4 | GT911 TP_IRQ — also the address strap, see below |
| 8 (SDA), 9 (SCL) | I2C: GT911 `0x5D`, io-extender `0x24` |
| 11 (CMD), 12 (CLK), 13 (D0) | TF card, **SDMMC 1-bit** |
| 15 (TX), 16 (RX) | RS485 terminal, unused so far |
| 19, 20 | native USB **or** CAN — the io-extender picks |
| **43, 44** | UART0 pads → FluidNC link (UART2 peripheral) |
| 26–37 | octal PSRAM + flash, unusable |
| 6 | genuinely free and unconnected. The only one. |

### io-extender (0x24)

Not a plain expander — a CH32V003 with a register map. `src/hw/io_expander.cpp`
reimplements Waveshare's driver because theirs seeds the output shadow with
`0xFF`, and bit 5 is `CAN_SEL`.

| Line | Function |
|---|---|
| EXIO1 | GT911 reset |
| EXIO2 | backlight enable (DISP) |
| EXIO3 | panel reset |
| EXIO4 | TF card DAT3 — **must idle high** for SDMMC mode |
| EXIO5 | `0` = USB-C is native USB, `1` = it becomes the CAN PHY |
| EXIO6 | panel VCOM rail |
| PWM reg `0x05` | backlight brightness, **inverted**: 0 is brightest |

---

## Flashing

Plug into the Type-C marked **USB** (not the one marked UART). That is the
native USB-Serial-JTAG peripheral on GPIO 19/20, and it is both the flashing
port and the debug console.

```bash
pio run -e ws7b -t upload
```

If the port does not enumerate, hold BOOT while plugging in, then release.

The board switch labelled UART routes GPIO 43/44 to *either* the CH343 or the
UART terminal. **Set it to the terminal and leave it.** The FluidNC link lives
there permanently and never has to be given up to update the board — which is
what the old UPDATE/PENDANT link mode existed to work around. That mode, its
NVS key and its Setup-page buttons are gone.

---

## What changed in the port

- **`platformio.ini`** — 16 MB flash, `partitions_16mb.csv`, `ARDUINO_USB_MODE=1`
  + `ARDUINO_USB_CDC_ON_BOOT=1`. Still the pioarduino platform: `espressif32@6.9.0`
  cannot add octal PSRAM to the heap, and the N16R8 has the same OPI PSRAM the
  CrowPanel did.
- **`src/hw/io_expander.{h,cpp}`** — new, replaces `lib/PCA9557` (deleted).
- **`src/hw/display_hw.cpp`** — new pin map, 1024×600, Waveshare's timings.
  Backlight is no longer a LovyanGFX `Light_PWM`; it is a register write.
- **`src/hw/touch.cpp`** — `gt911_reset()` added. The GT911 latches its I2C
  address from INT at the moment RST is released, and RST is on the extender
  while INT is a GPIO, so both have to be driven by hand and in order.
- **`src/hw/sdcard.cpp`** — `SD` over SPI became `SD_MMC` 1-bit. There is no CS
  on a GPIO to give the SPI library, and the slot is wired for SDMMC anyway.
- **`src/ui/theme.h`** — `SCREEN_W/H` now come from `display_hw.h` instead of
  being a second copy. `CONTENT_H` grew from 256 px to 376.
- **`src/ui/view_expanded.cpp`** — canvas and log-line count derive from the
  screen size rather than being 800×400 and 13.

---

## Things a future session should not have to rediscover

- **The first `ioexp_write()` publishes all eight bits.** Seeding the shadow
  byte with `0xFF` sends `CAN_SEL` high, which hands the USB-C to the CAN
  transceiver and disconnects the port you flash and debug through.
- **Panel timings are Waveshare's, not a generic ST7262 table.** A 162-clock
  hsync pulse looks wrong and is correct for this panel.
- **PCLK is the tuning knob.** 30 MHz × 2 bytes is 60 MB/s out of PSRAM while
  LVGL writes to it. Tearing, shearing or a rolling band → drop
  `LCD_PCLK_HZ` in `display_hw.cpp` to 21 MHz first.
- **`Wire.begin(8, 9)` must run before `ioexp_init()`**, which must run before
  `display_init()` — the extender gates the VCOM rail and the backlight.
- **No IMU on this board.** The CrowPanel had an ICM-20948; the 7B does not.
  `imu_begin()` reports absent and the vibration guard stays inert until one is
  plugged into the I2C terminal. Intended, not a bug.
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
| Board wiki | https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-7B |
| Vendor demos | https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7B |
| Build | `pio run -e ws7b` |
| Tests | `pio test -e native` (51 cases) |
| Repo | `https://git.yax.family/matthew/CNC-Disply` |

### Topology

```
Waveshare 7B ──UART 1M──► Airdale (passthrough) ──► Corgi (FluidNC)
```

The Airdale is a passthrough, not the controller. FluidNC runs on the Corgi, and
its SD card is unreachable inside a sealed box — which is why the panel's own
card is the G-code source.

### Still open

- **Layout has 47% more content height** (376 px vs 256) and 224 px more width.
  Nothing was redesigned to use it; the grid just reflowed. Worth a look once
  you can see it.
- **RS485 on GPIO 15/16 is unused.** It is the better long-cable link to the
  machine if the Corgi end ever gets a transceiver.
