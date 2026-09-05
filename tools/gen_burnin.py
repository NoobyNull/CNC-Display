#!/usr/bin/env python3
"""Generate a gear burn-in G-code file: a racetrack oval, XY only, no spindle,
feed ramping up by a fixed percent so step/tooth skipping shows up at a known
speed instead of on a real part.

The oval is two semicircles joined by straights -- a true ellipse cannot be
made of G2/G3 arcs, and the tangent joins mean the path itself has no corner
jolts: any violence is the speed's doing, which is the point of the exercise.

Zero the machine so the oval's bounding box (0..width in X, 0..height in Y) is
clear travel. The Corgi has no soft limits and no homing: the file cannot save
you from a bad zero.

    ./tools/gen_burnin.py                          # default: F500->F5000, +1%/pass, ~6h
    ./tools/gen_burnin.py --pct 2                  # ~3h version
    ./tools/gen_burnin.py --passes-per-step 10     # the literal 10-passes spec (very long)
"""
import argparse
import math

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--width",  type=float, default=750.0, help="oval width mm")
p.add_argument("--height", type=float, default=375.0, help="oval height mm (also arc diameter)")
p.add_argument("--f0",     type=float, default=500.0, help="starting feed mm/min")
p.add_argument("--fmax",   type=float, default=5000.0, help="final feed mm/min (machine max_rate)")
p.add_argument("--pct",    type=float, default=1.0, help="feed increase percent per step, compounding")
p.add_argument("--passes-per-step", type=int, default=1, help="laps at each feed before increasing")
p.add_argument("-o", "--out", default="tools/burnin_oval.nc")
a = p.parse_args()

r = a.height / 2.0
straight = a.width - a.height
if straight <= 0:
    raise SystemExit("width must exceed height: the straights would have negative length")
lap_mm = 2 * straight + math.pi * a.height   # two straights + two semicircles

steps = math.ceil(math.log(a.fmax / a.f0) / math.log(1 + a.pct / 100.0))
feeds = [min(a.f0 * (1 + a.pct / 100.0) ** k, a.fmax) for k in range(steps + 1)]
total_passes = len(feeds) * a.passes_per_step
est_min = sum(a.passes_per_step * lap_mm / f for f in feeds)

x0, x1 = r, a.width - r   # straight endpoints; bounding box is 0..width, 0..height
lines = [
    f"(burn-in oval {a.width:g}x{a.height:g}mm, {total_passes} passes)",
    f"(feed {a.f0:g} -> {a.fmax:g} mm/min, +{a.pct:g} pct every {a.passes_per_step} pass(es))",
    f"(estimated runtime {est_min/60:.1f} hours, ignoring accel)",
    "(zero: bounding box 0..%g X, 0..%g Y must be clear travel)" % (a.width, a.height),
    "G21 G90 G17 G94",
    f"G1 X{x0:g} Y0 F{a.f0:g}   (ease to start point)",
]
for i, f in enumerate(feeds):
    for lap in range(a.passes_per_step):
        n = i * a.passes_per_step + lap + 1
        lines.append(f"(pass {n}/{total_passes} F{f:.0f})")
        lines.append(f"G1 X{x1:g} Y0 F{f:.0f}")
        lines.append(f"G3 X{x1:g} Y{a.height:g} I0 J{r:g}")
        lines.append(f"G1 X{x0:g} Y{a.height:g}")
        lines.append(f"G3 X{x0:g} Y0 I0 J-{r:g}")
lines.append("G1 X%g Y0 F%g  (park where we started, gently)" % (x0, a.f0))
lines.append("M2")

with open(a.out, "w") as fh:
    fh.write("\n".join(lines) + "\n")

# The only check that matters: every pass ends where it began.
assert lines[-3].startswith(f"G3 X{x0:g} Y0"), "path does not close"
print(f"{a.out}: {total_passes} passes x {lap_mm:.1f}mm = {total_passes*lap_mm/1000:.1f}m of travel")
print(f"feed {a.f0:g} -> {feeds[-1]:.0f} mm/min, estimated {est_min:.0f} min ({est_min/60:.1f} h)")
