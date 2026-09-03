#!/bin/bash
# Bring up the whole bench: fake controller, web panel, simulated display.
#
#   ./sim/run.sh                 bring-up profile (the real Corgi: no limits)
#   ./sim/run.sh equipped        endstops, probe, coolant, door all present
#
# Ctrl-C, or closing the display window, tears both halves down.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$HERE")"
PROFILE="${1:-bringup}"
LINK_PORT="${LINK_PORT:-2323}"
WEB_PORT="${WEB_PORT:-8080}"

[ -x "$HERE/cncsim" ] || { echo "building the simulator first..."; make -C "$HERE" -j"$(nproc)"; }

python3 "$HERE/fluidnc_sim.py" --profile "$PROFILE" \
        --link-port "$LINK_PORT" --web-port "$WEB_PORT" &
SIM_PID=$!
# Without this the panel's first connect attempt races the listener and it
# spends its first two seconds in NO LINK for no reason.
sleep 1

cleanup() { kill "$SIM_PID" 2>/dev/null || true; wait "$SIM_PID" 2>/dev/null || true; }
trap cleanup EXIT INT TERM

echo "bench: http://127.0.0.1:$WEB_PORT"
cd "$ROOT"
"$HERE/cncsim" --port "$LINK_PORT" --sd "$HERE/sdcard"
