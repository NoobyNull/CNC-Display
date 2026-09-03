# Simulator bench

Runs the panel's **real** UI and model code on the desktop, against a fake
FluidNC that has an **actual machine config** behind it.

```bash
./sim/run.sh              # the real Corgi: no endstops, no probe
./sim/run.sh equipped     # same machine with limits, probe, coolant, door
```

Two things come up: an 800x480 SDL window (the panel) and
<http://127.0.0.1:8080> (the bench — NPN switch toggles, indicator lamps,
profile switch, controller log).

## Why it exists

The UI has to decide what to offer based on what the controller says it has.
That decision cannot be developed against a machine that is in the shop, and
`tools/fakefluidnc.py` answers `?` and echoes `ok` — it has no config, so it
cannot answer the only question that matters here.

`fluidnc_sim.py` loads `profiles/corgi_bringup.yaml`, which is the live Corgi's
`config.yaml` pulled from 192.168.0.5 on 2026-08-25, and derives everything from
it: which axes exist, their rates and accelerations, whether homing has a cycle,
whether a probe pin is set. `equipped.overlay.yaml` is merged on top for the
"fully equipped" profile — an overlay rather than a second copy, so re-pulling
the real config keeps both profiles current.

## What is real and what is not

| Real | Faked |
|---|---|
| `src/ui/*`, `src/model/*`, XModem — compiled unmodified | `src/hw/*` → SDL window, mouse-as-touch |
| LVGL v8.3.11, the exact version the firmware pins | The UART → a TCP socket to the sim |
| `src/lv_conf.h`, used as-is | NVS → an in-memory map (defaults every run) |
| The FluidNC v4.0.3 reply formats, from a live capture | Motion — constant rate, no accel ramp |

If a build here fails after a firmware change, a layer that is meant to be
portable grew a hardware dependency. That is the point of the arrangement.

## Screenshots without a desktop

```bash
sim/cncsim --page motion --shot /tmp/motion.bmp --shot-ms 6500
```

Works under `SDL_VIDEODRIVER=dummy`, so pages can be reviewed from a terminal.
`--shot-ms` has to clear the ~4.6 s splash timeline.

Pages: `jog home probe offsets run macro motion cal diag setup`.

## Checks

```bash
python3 sim/test_sim.py     # the fake controller answers what the panel expects
pio test -e native          # the parsers, including caps and filelist
```

`test_sim.py` asserts against the values captured from the live Corgi, so if it
passes, the sim is still telling the panel the truth.

## Layout

    fluidnc_sim.py           fake FluidNC v4.0.3: link on :2323, bench on :8080
    panel.html               the bench UI
    profiles/                corgi_bringup.yaml (real) + equipped.overlay.yaml
    sdcard/                  stands in for the panel's microSD
    src/                     hardware shims: display, touch, SD, link, main
    shim/                    Arduino.h, Preferences.h, esp_heap_caps.h
    deps/                    lvgl + GrblParser, fetched by `make deps`
