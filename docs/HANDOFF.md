# Handoff — CNC Console, 2026-08-11

The firmware moved from the Elecrow CrowPanel Advance 7.0" to the **Waveshare
ESP32-S3-Touch-LCD-7B**. The old board was abandoned because it could not be put
into download mode: its CH340's TX line was dead and EN was shorted to IO0. None
of that survives here — the 7B flashes over native USB.

---

## Session 2026-08-12 — read this first

Everything below the next divider is still accurate except where this section
contradicts it. **73/73 host tests pass**, flash 14.4%, RAM 20.6%.

### Added

| | |
|---|---|
| Screen blanking | SETUP -> "Screen off", 0-60min in 5s, **default Off** (opt-in) |
| Brightness | now 10-100% in 5% steps; storage stays 0-255 |
| Jog | hold a direction key for continuous motion at <=1mm; tap the pad centre to cycle the step x10 |
| CAL page | new nav slot: command 10/50/100mm, measure, apply corrected steps_per_mm |
| Sensor bars | SETUP thresholds are horizontal bars of live-vs-threshold; guard buttons 2x2; jerk histogram removed from the UI |
| Touch | now INT-driven (~7 reads/s, was 50); presses latched so a tap between two LVGL reads is never lost; stall watchdog with backoff |
| IMU | 200Hz -> 50Hz. **Do not go lower**: jerk is rate-independent arithmetic but bandwidth is not — 10Hz resolves 5Hz and a 20ms impact falls between samples, leaving the guard blind while still appearing armed |

### Two bugs shipped and fixed the same session — both worth remembering

**The backlight never came back.** `screen_idle_tick()` decided whether to touch
the backlight by comparing the screensaver's state before and after updating it,
but the touch driver clears that same flag when it swallows the wake tap. No
transition left to observe, `EXIO_DISP` never re-enabled, panel dark until a
power cycle. Fixed by tracking what the backlight *is* doing against what the
screensaver *wants*. **Never infer a hardware edge from state another layer can
also move.**

**A stuck touch killed the panel.** The wake-tap swallow waited for a release.
The GT911 holds its last reported state when it stops producing fresh scans, so
`touch_down` stayed asserted, the release never came, every sample was swallowed
for ever. Fixed with a 600ms cap on the swallow. **Any gate that can only be
opened by an event from hardware is a gate hardware can leave shut.**

Neither was caught by the host tests, and the first could not have been — the
pure module was correct both times, the faults were in the hardware follower.
The regression test for the second one fails against the old implementation.

The console line now carries `scroff=<n>min idle=<n>s [BLANK]`. That diagnostic
turned "I don't think the timeout worked" into a fact in one reading, twice.
Leave it in.

### Verified on hardware this session

- Blanking fires at exactly `idle=301s` on a 5min setting, and stays blank.
- Panel links to a real FluidNC v4.0.4 (the simulator) over the FTDI, locks
  115200, primes offsets and the file list, tracks state live.

### NOT verified

- **The CAL page has never talked to a controller.** `$/axes/<x>/steps_per_mm`
  read-back and apply are untested against anything real.
- **The jog changes have never moved a machine.**
- **The vibration bars have never seen an IMU.**

### Open questions left on the table

1. **Panel power.** Do **not** run the panel off the Airedale's RJ12 5V — that
   port was sized for a FluidDial (~100mA), and a 7" panel with backlight is
   500-800mA through 26AWG phone cable. ~0.5V drop at 800mA over 2m, and 5V has
   no headroom. Feed it at its own PH2.0 `POWER/GND/5V` input from a buck at the
   panel, **cut the RJ12 5V conductor**, keep RJ12 GND for the UART reference.
2. **File flow undecided.** The panel already does both directions —
   `upload_start()` sends `$XR=`, `pathload_start()` sends `$XS=`. If G-code
   lives on the controller's SD (put there over WiFi), nothing more is needed:
   list it and `$SD/Run=`. A panel-side copy needs a new sink in
   `path_loader.cpp:19`. Decide before building.
3. **USB thumb drive is a dead end.** The 7B's native USB-C has 5.1K pulldowns
   on CC1/CC2 (sink) and VBUS hard-tied to the 5V rail with no switch — it is
   wired as a device. Confirmed from the Waveshare schematic. A microSD
   extension ribbon solves the real problem; keep it **short**, the card runs
   SDMMC 1-bit at 20MHz (`sdcard.cpp:27`), not SPI.
4. **`JogCancel` (0x85) did not stop a jog on FluidNC v4.0.4** — neither raw
   (which is what `GrblParserC.c:572` sends) nor UTF-8 encoded, while `!` worked
   every time. Unexplained. Continuous jog therefore re-issues bounded chunks
   and never depends on that byte. Worth settling on real hardware.

### The GT911 stalls, and it is not cured

**The open bug.** After a burst of touch activity the GT911 stops setting its
data-ready bit and never resumes. Not a bus fault: `polls` climbs, `scans`
freezes, `lockfail=0`, `staterr=0` — the chip answers every transaction and
reports no frames. `touch_poll()` keeps the last state when no frame arrives, so
`touch_down` freezes and the panel looks dead. A power cycle clears it.

**Each watchdog reset yields exactly three more frames, then stalls again.**
103 -> 106 -> 109, every time. That regularity is the lead.

**Untried:** this driver has never written a GT911 config — no refresh rate, no
INT trigger mode — it runs on whatever the chip has stored, and we now drive INT
as an interrupt without knowing what mode it thinks it is in. Read
`0x8047..0x80FF` and check the INT trigger byte at `0x804D` before writing
anything; a bad checksum at `0x80FF` bricks touch until the chip is re-flashed.

**Ruled out, with evidence — do not re-test:** bus wedging (`staterr=0`), the IMU
starving the bus (`lockfail=0`), a GPIO4/RGB conflict (RGB uses
14/38/18/17/10/39/0/45/48/47/21/1/2/42/41 + 46/3/5/7), screen blanking
(reproduced seconds after boot, screen lit), the screensaver swallowing input
(`idle` climbs during the stall), and the interrupt conversion itself.

**Two wrong turns worth not repeating.** "Idle silence is normal for a GT911" is
false — a healthy chip produces ~5-10 frames/s untouched. That was concluded from
a chip already wedged, and a watchdog was narrowed to the wrong condition on the
strength of it. *Never characterise normal behaviour from the device you are
debugging.* And the first watchdog storm-reset every 5s at 300ms of blocked UI
each; it now backs off after three futile attempts.

Full write-up: `/tmp/HANDOFF-display-responsiveness-fix.md`.

### The simulator rig is broken

`~/Projects/FluidNC/simrun` no longer answers commands on the pty or a pipe. It
boots fully (config parses, `fault_pin`/`estop_pin` reported) then loops on
`Emulator - Error: bind failed` after `HTTP started on port 80`. Root cause not
isolated. It was working earlier the same day; the confound was three orphaned
`program` instances fighting over ports 80/23, and clearing them did **not**
restore it.

`ttybridge.py` gained `--serial <port> --baud <n>` to serve the pty and a real
UART at once. The pty half stopped delivering once the panel joined. Suspect
that change before suspecting the simulator.

**Kill simulator processes by PID, never by pattern.** `pkill -f`, and any
`/proc` scan matching a string that also appears in your own command line, kills
the agent session (exit 144). Happened again this session.

---

## Status

**First hardware bring-up is done.** The panel draws, touch works, the UI is up
and interactive. `13.6%` flash of a 6 MB app partition, `20.6%` RAM, 58/58 host
tests pass.

The jog acceptance test passes: press a jog key, slide off without lifting, the
axis stops (`LV_EVENT_PRESS_LOST`). That only worked after the GT911 press-state
fix below — before it, the press never survived long enough to be lost.

**The link is proven, against a stand-in controller.** An FTDI on the UART
terminal answering `?` with a 4-axis Grbl status report brings the panel fully
up: it locks the baud, leaves `Disconnected`, fires `$#` and
`$Files/ListGCode=/sd` from `prime_link_if_up()`, and the DRO grows its **A
column and tracks it live**. So the transport, GrblParserC, the priming path and
the multi-axis layout are all confirmed — only the real machine at the far end
is untested.

Reproduce it with `fakefluidnc.py` (see below): it answers status polls and
pushes a report every 200ms, which is what `$RI=200` asks for. Without the
periodic push the panel decides the link went away.

**Still not verified against hardware:**

- **The real controller.** See the Airedale baud/parity note under *Still open* —
  the stand-in worked because it spoke 115200 8N1, which is what the panel
  already hunts for. The Corgi as configured does not.
- **The vibration guard.** No IMU has ever successfully sat on this bus, so the
  jerk histogram, thresholds and smoothed peak are covered by host tests only.
  The one that was wired held an I2C line low and took out all touch until it
  was unplugged — see below.

---

## Bringing the panel up with no machine attached

An FTDI on the UART terminal is enough to exercise everything above the wire.
`tools/fakefluidnc.py` answers `?` with a 4-axis report and `ok` to every line,
so `send_line()` never spins in `_ackwait`:

```bash
python3 tools/fakefluidnc.py /dev/ttyUSB0 115200 120
```

Two things that cost time working this out:

- **"Wired UART" persists in NVS.** With the Setup toggle off, `s_active` stays
  `TRANSPORT_NONE` and `fnc_putchar()` writes nothing at all — the panel is
  silent on the wire and it looks exactly like miswiring. Check the toggle before
  suspecting TX/RX.
- **The UART1/UART2 switch** routes the one UART peripheral to a single PH2.0
  terminal. The other terminal is electrically dead. Waveshare's docs describe
  the switch as selecting between the two terminals, *not* between the CH343 and
  a terminal as an earlier revision of this document claimed.
- **Which Type-C is which:** `303a:1001` is the native USB (flashing + `Serial`
  console); `1a86:55d3` is the CH343 USB-UART. The VID tells you which port the
  cable is in when the silkscreen does not.

---

## Bring-up: what actually went wrong

Five days of assumptions died on contact with the hardware. In order of how long
each cost:

### The panel was white because LovyanGFX cannot express these timings

`Bus_RGB::config_t` stores every porch and pulse width in an **`int8_t`**
(`Bus_RGB.hpp:85-90`, still true on upstream master). Waveshare's real values —
`hsync_pulse_width 162`, `hsync_back_porch 152` — wrap to **-94** and **-104**.
The horizontal timing generator gets garbage, the panel never syncs, and the
screen sits white with the backlight lit.

Everything else checked out and wasted hours: the pin map matches Waveshare's
`rgb_lcd_port.h` exactly, so do the timings, the PCLK and the io-extender.

### Then it was static, because LovyanGFX has no bounce buffer

`Panel_RGB::initFrameBuffer()` unconditionally puts the whole framebuffer in
PSRAM — its `use_psram` field is literally marked `unimplemented`, and the code
comments itself as "provisional implementation ... ToDo: split allocation".
Streaming 1.2 MB per frame straight out of PSRAM while LVGL writes into it
produces static-TV noise. Dropping PCLK to 21 MHz reduced it; nothing removed it.

**The driver is now ESP-IDF `esp_lcd_rgb_panel` directly** (`display_hw.cpp`):
`uint32_t` timings so the true 162/152 are restored, two DRAM bounce buffers
(`1024*10` px), full 30 MHz. LovyanGFX is no longer in the display path.

### Touch worked all along; we were throwing the press away

`touch_poll()` treated GT911 status `0x00` as a finger lift. It means "no fresh
scan since you last cleared the buffer" — the normal case, since we poll at 50Hz
and the controller refreshes slower. `touch_down` was dropping to false between
refreshes, flickering the press faster than LVGL samples the input device.
Press-and-hold on a jog key was impossible. Now the previous state stands unless
bit 7 is set.

### An SD card can stop the board booting entirely

A faulty microSD in the slot pulls the 3.3 V rail down hard enough that the chip
never starts — **no USB enumeration at all**, which is below firmware and below
boot mode. Symptom is indistinguishable from a dead board or a charge-only cable.
A known-good card behaves perfectly. If the panel is completely dark, pull the
card before suspecting anything else.

### A miswired I2C device takes out every input on the panel

An ICM-20948 wired wrong held a line low and wedged the bus. `ESP_ERR_INVALID_STATE`
on every transaction from then on — and since the GT911 shares SDA 8 / SCL 9,
**all touch died, safety bar included**. `touch_poll()` now counts consecutive
failures and re-inits the bus after 25 (~0.5 s), and `imu_probe()` recovers the
bus after a failed probe, because SparkFun's `begin()` against a missing device
is what wedges it in the first place.

---

## WiFi was removed

The WiFi/Telnet transport is gone: `wifi_link.{h,cpp}` deleted, `TRANSPORT_WIFI`,
the Telnet host/port settings and the Setup credentials UI with it.

The ESP32-S3's WiFi stack blacks out the flash cache, and arduino-esp32 ships its
prebuilt libs with **`CONFIG_LCD_RGB_ISR_IRAM_SAFE` unset** — so the RGB panel's
ISR cannot run during those blackouts, the DMA loses sync and the display tears
and drifts. Bounce buffers do not cover it. It cost 623 KB of flash and the
machine link is a cable anyway.

`display_resync()` survives that removal and is called after every **NVS write**,
which blanks the cache the same way — the brightness +/- buttons write a key per
press.

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
| Tests | `pio test -e native` (65 cases) |
| Repo | `https://git.yax.family/matthew/CNC-Disply` |

### Topology

```
Waveshare 7B ──UART 1M──► Airdale (passthrough) ──► Corgi (FluidNC)
```

The Airdale is a passthrough, not the controller. FluidNC runs on the Corgi, and
its SD card is unreachable inside a sealed box — which is why the panel's own
card is the G-code source.

### Layout: everything derives from the panel now

The port left the CrowPanel's constants in place and the extra 120 px showed as
dead space on every page. All of it is now computed from `CONTENT_H` / `SCREEN_H`
rather than fixed, so a future panel reflows on its own:

| Was | Now | Where |
|---|---|---|
| `NAV_VISIBLE 5` | 6 | `shell.cpp` — 5 exactly filled the 480px CrowPanel |
| `ROWS 3` (files) | 4 | `page_run.cpp` |
| `ROWS 5` (settings) | 8 | `page_diag.cpp` |
| `VISIBLE_ROWS 6` (WCS) | 9 | `page_offsets.cpp` — G54–G59 with no scrolling |
| `PAD = U*30` | `CONTENT_H - U*2` | `page_jog.cpp` |

The derivation reproduces every one of the old constants exactly when fed the
CrowPanel's 240px page — which is how we know they were stale rather than chosen.

**The DRO follows `n_axes` from FluidNC** instead of clamping to 3, so the Corgi's
A axis appears on its own. Readout font steps 40 → 28 → 20 px as cells narrow,
picked off cell width, since a 40px `-1234.567` clips at four columns.

### Still open

- **RS485 on GPIO 15/16 is unused.** It is the better long-cable link to the
  machine if the Corgi end ever gets a transceiver.
- ~~**`corgi_config.yaml:184`** passthrough baud mismatch~~ — **not a mismatch.**
  `passthrough_baud`/`passthrough_mode` are read only by `Uart::enterPassthrough()`
  (`Uart.cpp:99`), reached only via the `$Uart/Passthrough` command, which
  forwards firmware uploads through the Corgi to a downstream MCU. Normal pendant
  traffic uses `uart2`'s own `baud: 1000000` / `mode: 8N1` — exactly what
  `transport.cpp:12` probes first. 57600/8E1 is the Airedale's reflash rate and
  should stay.
- **The keyboard is a singleton** — `w_keyboard()` returns one object and both
  `page_diag.cpp` and `page_setup.cpp` used to attach their own `kb_done_cb` to
  it. Setup's is gone with the WiFi fields; if a second page ever needs the
  keyboard, the handlers will fight.
- **LVGL's shift key is labelled `ABC`**, bottom-left of the second row. Not
  discoverable. Custom keymaps would fix it, ~20 lines.
- **CNC-TW still escalates to e-stop on jerk alone** (see above). Not fixed
  there. Its `smoothed_peak`, `rms_rate` and `margin` feed a 5-level feed-override
  ladder this panel does not have — only `smoothed_peak` was ported.
- **`xtensa/config/core.h` includes `"../hal.h"`, which does not exist.** GCC
  survives on search-path fallback; clangd does not, so every file reaching
  `<Arduino.h>` shows one editor-only error. The build is unaffected.
