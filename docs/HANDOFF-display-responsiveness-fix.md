---
status: waiting
created: 2026-08-12
focus: Cure the GT911 touch stall on the CNC panel — the chip stops producing frames after a touch burst while answering I2C perfectly; reading its stored config is the untried lead
---

# Handoff — display responsiveness / GT911 stall

Panel: Waveshare ESP32-S3-Touch-LCD-7B. Repo `~/Projects/CNC-Disply`, pushed to
Gitea, clean at `540f304`. Flash 14.4%, RAM 20.6%, 73/73 host tests pass.

The panel is **usable but not trustworthy**: it works, then loses touch, and a
power cycle clears it. Everything else in this session is done and committed.

## The bug, precisely

After a burst of touch activity ends, the GT911 **stops setting its data-ready
bit (0x814E bit 7)** and never resumes.

It is not a bus fault. While stalled:

```
[touch] down=0 scans=103 polls=162 lockfail=0 staterr=0 resets=0
[touch] down=0 scans=103 polls=181 lockfail=0 staterr=0 resets=0
[touch] down=0 scans=106 polls=202 lockfail=0 staterr=0 resets=1
```

`polls` climbs, `scans` freezes, **zero** I2C errors, **zero** mutex failures.
The chip answers every transaction and reports no new frames.

`touch_poll()` keeps the previous state when no frame arrives, so `touch_down`
freezes at whatever it last held and the panel looks dead.

### The clue nobody has followed yet

**Each watchdog reset yields exactly three more frames, then stalls again.**
103 -> 106 -> 109. Every time. That regularity is not chance.

### The lead

**This driver has never written a GT911 configuration.** No refresh rate, no INT
trigger mode — it runs on whatever the chip has in its own storage, and as of
this session we drive INT as an interrupt without knowing what mode the chip
thinks it is in. If its stored config expects a different INT polarity or a host
acknowledgement we are not giving, "a few frames then waiting for ever" is
exactly what you would expect.

**Next step: read `0x8047`..`0x80FF` and inspect the INT trigger mode byte at
`0x804D` (bits 0-1) before writing anything.** Do not write a config blind — a
bad checksum at 0x80FF bricks touch until the chip is re-flashed.

## Ruled out, with evidence — do not re-test these

| Theory | Evidence against |
|---|---|
| I2C bus wedged | `staterr=0` throughout the stall |
| IMU starving touch on the shared bus | `lockfail=0` throughout, even at 200Hz |
| GPIO4 (INT) conflicting with the RGB panel | RGB uses 14/38/18/17/10/39/0/45/48/47/21/1/2/42/41 + 46/3/5/7. GPIO4 is not among them |
| Screen blanking causing it | Reproduced seconds after boot with the screen lit |
| The screensaver swallowing input | `idle` climbs during the stall; nothing is being swallowed |
| Interrupt conversion fixing it | Converted this session. Stall reproduces identically |

## Two wrong turns, so they are not repeated

**"Idle silence is normal for a GT911."** It is not — a healthy chip produces
~5-10 frames/s untouched. That conclusion was drawn from a chip that was already
wedged, and the watchdog was narrowed to the wrong condition on the strength of
it. **Never characterise normal behaviour from the device you are debugging.**

**The first watchdog storm-reset every 5s** at 300ms of blocked UI each, because
it treated absent frames as a fault without asking whether resetting had ever
helped. It now backs off after three futile attempts and says so.

## What was changed this session

- **Touch reads on INT** (`GPIO4`, `CHANGE`), ISR raises a flag only. ~7 reads/s
  where there were 50. A 100ms idle heartbeat remains on purpose: interrupts are
  the latency path, not the only path, and the stall watchdog needs a heartbeat
  to measure against.
- **Presses are latched** — a tap can start and finish between two LVGL reads
  (touch 50Hz, LVGL 30ms, nothing keeps them in phase) and was simply lost. This
  is what fixed "buttons sometimes do not register".
- **IMU 200Hz -> 50Hz.** `getAGMT()` reads ~23 bytes; at 200Hz it held the bus
  ~12% of wall clock from inside `loop()`, delaying `lvgl_tick()`. **Do not go
  below 50.** Jerk uses the real interval so the arithmetic is rate-independent,
  but bandwidth is not: 50Hz resolves 25Hz, 10Hz resolves 5Hz and a 20ms impact
  falls between samples — blind while still appearing armed.
- **Stall watchdog with backoff**, and `[touch] down/scans/polls/lockfail/
  staterr/resets` on the console every 2s. Keep that line; it settled three
  wrong theories in one reading each.

## Working on it

```bash
cd ~/Projects/CNC-Disply
pio run -e ws7b                                    # build
pio run -e ws7b -t upload --upload-port /dev/ttyACM0
pio test -e native                                 # 73 cases
```

- **A serial monitor holding `/dev/ttyACM0` makes the upload fail.** Kill it
  first. Happened this session.
- **Kill background processes by PID, never by pattern.** `pkill -f`, and any
  `/proc` scan matching a string that also appears in your own command line,
  kills the agent session (exit 144). Happened twice across two sessions.

## Everything else, for context

`docs/HANDOFF.md` in the repo opens with a full section for this session. In
brief, also landed and pushed: screen blanking (opt-in, verified blanking at
301s), brightness in percent, hold-to-jog continuous at <=1mm with the pad
centre cycling the step, a CAL page for steps/mm, and the sensor thresholds
redrawn as live-vs-threshold bars.

**Never tested against a real controller:** the CAL page, the jog changes, the
vibration bars. That is the highest-value thing to do once a link exists.

**The FluidNC simulator rig is broken** (`~/Projects/FluidNC/simrun`) — boots,
then answers nothing, looping on `Emulator - Error: bind failed`. Suspect the
`--serial` addition to `ttybridge.py` before suspecting the simulator.

**Machine-side work is a separate handoff**, now versioned at `~/Desktop/CNC`
(local git, not pushed) with its own `HANDOFF.md`. Two hardware blockers stand
before first power-up: the Corgi is 12V only while `cnc-wiring.png` feeds it
24V, and 24V relay coils sit on MOSFETs whose flyback clamps to a 12V VMot.

## Safety note to carry forward

The safety bar lives behind this touch input. **Losing touch means no on-screen
HOLD and no RESET on a running machine.** Until the stall is cured, that is an
argument for keeping a hardware e-stop within reach and not relying on the panel
as the stop of last resort — which was already true, and is more true now.
