#!/bin/bash
# The whole bench, ONE app: fluidnc_sim.py serves the bench page, spawns the
# LVGL panel headless behind it, and embeds its live screen in the page.
#
#   ./sim/run.sh                 bring-up profile (the real Corgi: no limits)
#   ./sim/run.sh equipped        endstops, probe, coolant, door all present
#   WINDOW=1 ./sim/run.sh        also open the panel's SDL window
#
# Ctrl-C tears everything down.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROFILE="${1:-bringup}"

[ -x "$HERE/cncsim" ] || { echo "building the simulator first..."; make -C "$HERE" -j"$(nproc)"; }

EXTRA=()
[ "${WINDOW:-0}" = 1 ] && EXTRA+=(--windowed)

exec python3 "$HERE/fluidnc_sim.py" --profile "$PROFILE" \
     --link-port "${LINK_PORT:-2323}" --web-port "${WEB_PORT:-8080}" \
     "${EXTRA[@]}"
