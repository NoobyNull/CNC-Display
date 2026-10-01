#!/usr/bin/env python3
"""A FluidNC v4.0.3 stand-in with a real config behind it, plus a web bench.

tools/fakefluidnc.py answers '?' and echoes 'ok'. That was enough to prove the
DRO moved; it is not enough to develop a config-driven UI against, because the
whole question the panel needs answered -- "does this machine HAVE endstops, a
probe, coolant?" -- lives in the config, and the old fake has no config.

This one loads an actual FluidNC config.yaml (profiles/corgi_bringup.yaml is
the live Corgi, pulled 2026-08-25) and derives everything from it:

  - $S emits the v4.0.3 Grbl-compat settings, generated from the config. NOTE
    v4 does NOT expose the config tree here the way v3 did -- there is no
    /probe/pin in $S. That is the real firmware's behaviour, reproduced on
    purpose, because the panel currently looks for exactly that key.
  - $CD dumps the YAML, which IS where probe/coolant/control pins live.
  - $H errors with 5 (homing not enabled) when no homing cycle is configured.
  - Hard limits only alarm when the config actually enables them.

Two ports:
  :2323  the machine link, line protocol, what the LVGL sim connects to
  :8080  the web bench -- NPN switch toggles, indicator lamps, profile switch

Nothing here talks to real hardware. It is a bench fixture.
"""
import argparse
import atexit
import copy
import json
import math
import queue
import os
import re
import socket
import socketserver
import subprocess
import threading
import time
import urllib.request
from http.server import BaseHTTPRequestHandler, HTTPServer

import yaml

HERE = os.path.dirname(os.path.abspath(__file__))
PROFILES = os.path.join(HERE, "profiles")

AXES = "XYZABC"


def deep_merge(base, over):
    """Overlay dict-merge, so equipped.overlay.yaml rides on the real config."""
    out = copy.deepcopy(base)
    for k, v in (over or {}).items():
        if isinstance(v, dict) and isinstance(out.get(k), dict):
            out[k] = deep_merge(out[k], v)
        else:
            out[k] = copy.deepcopy(v)
    return out


def is_pin(v):
    """A configured pin. FluidNC writes an absent pin as NO_PIN, not null."""
    if v is None:
        return False
    s = str(v).strip()
    return bool(s) and s.upper() != "NO_PIN"


# The non-Grbl NVS settings of the v4.0.3 $S set (live Corgi, 2026-08-25).
# Writes are kept in Machine.nvs so the panel's re-read shows them.
NVS_DEFAULTS = (
    ("Notification/Type", "NONE"), ("Notification/T1", ""),
    ("Notification/T2", ""), ("Notification/TS", ""),
    ("Telnet/Enable", "ON"), ("Telnet/Port", "23"),
    ("HTTP/BlockDuringMotion", "ON"), ("HTTP/Enable", "ON"),
    ("HTTP/Port", "80"), ("MDNS/Enable", "ON"),
    ("WiFi/PsMode", "None"), ("WiFi/Mode", "STA>AP"),
    ("Sta/Password", "********"), ("Sta/MinSecurity", "WPA2-PSK"),
    ("WiFi/FastScan", "OFF"), ("Sta/IPMode", "Static"),
    ("Sta/IP", "192.168.0.5"), ("Sta/Gateway", "192.168.0.1"),
    ("Sta/Netmask", "255.255.255.0"), ("AP/Country", "US"),
    ("AP/SSID", "FluidNC"), ("AP/Password", "********"),
    ("AP/IP", "192.168.0.5"), ("AP/Channel", "5"),
    ("Hostname", "fluidnc"), ("Sta/SSID", "YAX-Net"),
    ("GCode/Echo", "OFF"),
    ("Start/Message", r"Grbl \V [FluidNC \B (\X) \H]"),
    ("Firmware/Build", ""), ("SD/FallbackCS", "-1"),
    ("Report/Status", "1"), ("Config/Filename", "config.yaml"),
    ("Message/Level", "Info"),
)


class Machine:
    """Config-derived capabilities plus live motion state."""

    def __init__(self, base, overlay=None, profile="bringup"):
        self.lock = threading.RLock()
        self.profile = profile
        self.cfg = deep_merge(base, overlay) if overlay else copy.deepcopy(base)
        self.log = []
        self._derive()
        self._reset_state()

    # ---- config -> capabilities -------------------------------------------

    def _derive(self):
        cfg = self.cfg
        # Every *_pin the config declares, as (dotted path, pin), sorted by
        # pin number — the bench shows this next to the switches so nobody
        # has to open the YAML (or squint at the board) to see what's where.
        self.pin_map = []

        def walk(node, path):
            for k, v in (node or {}).items():
                if isinstance(v, dict):
                    walk(v, path + (k,))
                elif k.endswith("_pin") and is_pin(v):
                    self.pin_map.append(
                        {"path": ".".join(path + (k,)), "pin": str(v)})
        walk(cfg, ())

        def pin_key(e):
            m2 = re.search(r"(\d+)", e["pin"])
            return (e["pin"].split(".")[0], int(m2.group(1)) if m2 else -1)
        self.pin_map.sort(key=pin_key)

        ax = cfg.get("axes", {}) or {}
        # Config order is authoritative: FluidNC indexes axes by their position
        # in the canonical XYZABC order, not by how the YAML happens to sort.
        self.axis_names = [a for a in AXES if a in ax]
        self.n_axes = len(self.axis_names)

        self.axis = {}
        for a in self.axis_names:
            d = ax.get(a) or {}
            motors = [d.get(f"motor{i}") for i in (0, 1)]
            motors = [m for m in motors if isinstance(m, dict)]
            homing = d.get("homing") if isinstance(d.get("homing"), dict) else None
            self.axis[a] = {
                "steps_per_mm": float(d.get("steps_per_mm", 100.0)),
                "max_rate": float(d.get("max_rate_mm_per_min", 1000.0)),
                "accel": float(d.get("acceleration_mm_per_sec2", 100.0)),
                "max_travel": float(d.get("max_travel_mm", 100.0)),
                "soft_limits": bool(d.get("soft_limits", False)),
                "homing_cycle": int(homing.get("cycle", -1)) if homing else -1,
                "homing_positive": bool(homing.get("positive_direction", False)) if homing else False,
                "motors": [
                    {
                        "hard_limits": bool(m.get("hard_limits", False)),
                        "limit_neg": is_pin(m.get("limit_neg_pin")),
                        "limit_pos": is_pin(m.get("limit_pos_pin")),
                        "limit_all": is_pin(m.get("limit_all_pin")),
                    }
                    for m in motors
                ],
            }

        probe = cfg.get("probe") or {}
        ctrl = cfg.get("control") or {}
        cool = cfg.get("coolant") or {}
        sd = cfg.get("sdcard") or {}
        start = cfg.get("start") or {}

        self.caps = {
            "axes": "".join(self.axis_names),
            "homing": any(v["homing_cycle"] >= 0 for v in self.axis.values()),
            "hard_limits": any(m["hard_limits"] for v in self.axis.values() for m in v["motors"]),
            "soft_limits": any(v["soft_limits"] for v in self.axis.values()),
            "probe": is_pin(probe.get("pin")),
            "toolsetter": is_pin(probe.get("toolsetter_pin")),
            "flood": is_pin(cool.get("flood_pin")),
            "mist": is_pin(cool.get("mist_pin")),
            "safety_door": is_pin(ctrl.get("safety_door_pin")),
            "estop": is_pin(ctrl.get("estop_pin")),
            "feed_hold_btn": is_pin(ctrl.get("feed_hold_pin")),
            "cycle_start_btn": is_pin(ctrl.get("cycle_start_pin")),
            "macros": [i for i in range(4) if is_pin(ctrl.get(f"macro{i}_pin"))],
            "sdcard": is_pin(sd.get("cs_pin")),
            "spindle": self._spindle_name(),
            "must_home": bool(start.get("must_home", False)),
        }

        # Which limit lamps exist, in the panel's own lamp order: motor0 as
        # lowercase X..C then motor1 as X2..C2, matching page_diag.cpp.
        lamps = []
        for i, a in enumerate(self.axis_names):
            for mi, m in enumerate(self.axis[a]["motors"]):
                if m["limit_neg"] or m["limit_pos"] or m["limit_all"]:
                    lamps.append(f"{a}{'' if mi == 0 else '2'}")
        self.limit_lamps = lamps

        self.switches = {name: False for name in lamps}
        if self.caps["probe"]:
            self.switches["P"] = False
        if self.caps["toolsetter"]:
            self.switches["T"] = False
        if self.caps["safety_door"]:
            self.switches["D"] = False
        if self.caps["estop"]:
            self.switches["E"] = False
        # Stepper-driver "online" inputs (Airedale 0-4: X Y1 y2 Z A), ridden
        # into Pn: as digit chars. Default ON — a healthy bench — and toggled
        # like any other input to fake a fault. M64/M65 on outputs 8-11 also
        # drive them, so the panel's RESET pulse round-trips.
        for d in "01234":
            self.switches[d] = True

    def _spindle_name(self):
        for key in ("H100", "Huanyang", "PWM", "10V", "Laser", "relay", "NoSpindle"):
            if isinstance(self.cfg.get(key), dict) or key in self.cfg:
                return key
        return None

    def _reset_state(self):
        n = self.n_axes
        self.state = "Idle"
        self.mpos = [0.0] * n
        self.wco = [0.0] * n
        self.target = list(self.mpos)
        self.feed_target = 0.0
        self.homed = False
        self.alarm = 0
        self.spindle_rpm = 0
        self.spindle_on = False
        self.flood = False
        self.mist = False
        self.ov = [100, 100, 100]      # feed, rapid, spindle
        self.prb = [0.0] * n
        self.prb_ok = 0
        self.probing = False
        self.sd_run = None             # (name, total_lines, sent_lines)
        self.report_ms = 75
        self.last_move = time.time()
        self.tlo = 0.0
        self.g92 = [0.0] * n
        self.wcs = "G54"
        self.offsets = {
            "G54": [0.0] * n, "G55": [0.0] * n, "G56": [0.0] * n,
            "G57": [0.0] * n, "G58": [0.0] * n, "G59": [0.0] * n,
        }

    def note(self, msg):
        self.log.append(f"{time.strftime('%H:%M:%S')} {msg}")
        del self.log[:-200]

    # ---- $S ----------------------------------------------------------------

    def settings_lines(self):
        """The v4.0.3 $S set, generated from the config.

        Captured from the live Corgi on 2026-08-25. The ordering and the exact
        key names matter: the panel string-matches them.
        """
        L = []
        A = lambda k, v: L.append(f"${k}={v}")
        A("Grbl/SoftLimitsEnable", int(self.caps["soft_limits"]))
        A("Grbl/HardLimitsEnable", int(self.caps["hard_limits"]))
        A("Grbl/HomingCycleEnable", int(self.caps["homing"]))
        dirs = 0
        for i, a in enumerate(self.axis_names):
            if self.axis[a]["homing_positive"]:
                dirs |= 1 << i
        A("Grbl/HomingDirections", dirs)
        A("Grbl/MaxSpindleSpeed", int(self._max_rpm()))
        A("Grbl/LaserMode", 0)
        for key, field, fmt in (
            ("Resolution", "steps_per_mm", "{:.3f}"),
            ("MaxRate", "max_rate", "{:.3f}"),
            ("Acceleration", "accel", "{:.3f}"),
            ("MaxTravel", "max_travel", "{:.3f}"),
        ):
            for a in self.axis_names:
                A(f"Grbl/{key}/{a}", fmt.format(self.axis[a][field]))
        nvs = getattr(self, "nvs", {})
        for k, v in NVS_DEFAULTS:
            A(k, nvs.get(k, v))
        return L

    def _max_rpm(self):
        sp = self.cfg.get(self.caps["spindle"] or "", {})
        if isinstance(sp, dict) and sp.get("max_RPM"):
            return sp["max_RPM"]
        return 24000

    def write_setting(self, name, value):
        """$Grbl/... writes. Returns None on success or an error string."""
        # FluidNC's INT_PROXY settings are read-only views of config.yaml.
        if name in ("Grbl/LaserMode", "Grbl/MaxSpindleSpeed", "Grbl/HomingDirections",
                    "Grbl/HomingCycleEnable", "Grbl/HardLimitsEnable", "Grbl/SoftLimitsEnable"):
            return "error:162"
        m = re.match(r"^Grbl/(Resolution|MaxRate|Acceleration|MaxTravel)/([XYZABC])$", name)
        if m:
            field = {"Resolution": "steps_per_mm", "MaxRate": "max_rate",
                     "Acceleration": "accel", "MaxTravel": "max_travel"}[m.group(1)]
            a = m.group(2)
            if a not in self.axis:
                return "error:3"
            try:
                v = float(value)
            except ValueError:
                return "error:5"
            # Real FluidNC rejects a non-positive rate outright; a zero here
            # would divide-by-zero the motion integrator below.
            if v <= 0:
                return "error:5"
            self.axis[a][field] = v
            self.note(f"set {name} = {v}")
            return None
        if name == "Report/Interval":
            self.report_ms = max(10, int(float(value)))
            return None
        if name in dict(NVS_DEFAULTS):
            self.__dict__.setdefault("nvs", {})[name] = value
            self.note(f"set {name} = {value}")
            return None
        return "error:3"

    # ---- reports -----------------------------------------------------------

    def pin_field(self):
        """Pn: -- the active input pins, exactly as FluidNC reports them.

        The web bench's toggles are the LOGICAL state (switch made), which is
        what a ':low:pu' NPN sensor produces when it sinks the line. The pin
        spec's inversion is already accounted for; there is no second flip.
        """
        s = ""
        for lamp in self.limit_lamps:
            if self.switches.get(lamp):
                s += lamp[0]
        for k in ("P", "T", "D", "E"):
            if self.switches.get(k):
                s += {"P": "P", "T": "T", "D": "D", "E": "R"}[k]
        # Only the four macro-pin drivers push into Pn: ('0'-'3', per the
        # 2026-09-07 contract); A ('4') is a user_input read via M66 and
        # never appears here.
        for d in "0123":
            if self.switches.get(d):
                s += d
        return s

    def status(self):
        with self.lock:
            st = self.state
            if st == "Run" and self.sd_run:
                st = "Run"
            mp = ",".join(f"{v:.3f}" for v in self.mpos)
            wc = ",".join(f"{v:.3f}" for v in self.wco)
            out = f"<{st}|MPos:{mp}|FS:{int(self.feed_target)},{self.spindle_rpm}"
            out += f"|WCO:{wc}"
            pn = self.pin_field()
            if pn:
                out += f"|Pn:{pn}"
            if self.ov != [100, 100, 100]:
                out += f"|Ov:{self.ov[0]},{self.ov[1]},{self.ov[2]}"
            if self.sd_run:
                name, total, sent = self.sd_run
                out += f"|SD:{100.0 * sent / max(1, total):.2f},{name}"
            return out + ">"

    # ---- motion ------------------------------------------------------------

    def tick(self):
        """Integrate motion. Coarse on purpose -- this is a UI bench, not a
        motion planner. Constant rate, no accel ramp; acceleration is modelled
        only as a config value the panel reads and writes."""
        with self.lock:
            now = time.time()
            dt = min(0.25, now - self.last_move)
            self.last_move = now
            if self.state in ("Alarm", "Hold", "Door"):
                self.feed_target = 0.0
                return

            # An estop or door switch takes precedence over whatever is moving.
            if self.switches.get("E"):
                if self.state != "Alarm":
                    self.state, self.alarm = "Alarm", 2
                    self.note("ESTOP input active -> Alarm 2")
                return
            if self.switches.get("D") and self.state not in ("Door",):
                self.state = "Door"
                self.note("safety door open -> Door")
                return

            delta = [self.target[i] - self.mpos[i] for i in range(self.n_axes)]
            dist = math.sqrt(sum(d * d for d in delta))
            if dist < 1e-4:
                self.mpos = list(self.target)
                self.feed_target = 0.0
                if self.state in ("Jog", "Run", "Home"):
                    if self.state == "Home":
                        self.homed = True
                        self.note("homing complete")
                    if self.sd_run:
                        name, total, sent = self.sd_run
                        if sent >= total:
                            self.sd_run = None
                            self.note("job complete")
                    self.state = "Idle"
                if self.probing:
                    self._finish_probe(hit=False)
                return

            rate = min(self.axis[a]["max_rate"] for a in self.axis_names)
            rate *= self.ov[0] / 100.0
            step = (rate / 60.0) * dt
            self.feed_target = rate
            if step >= dist:
                self.mpos = list(self.target)
            else:
                for i in range(self.n_axes):
                    self.mpos[i] += delta[i] * (step / dist)

            if self.probing and self.switches.get("P"):
                self._finish_probe(hit=True)
                return

            if self.caps["hard_limits"]:
                for i, a in enumerate(self.axis_names):
                    if self.switches.get(a) and any(m["hard_limits"] for m in self.axis[a]["motors"]):
                        self.state, self.alarm = "Alarm", 1
                        self.target = list(self.mpos)
                        self.note(f"hard limit {a} while moving -> Alarm 1")
                        return

            if self.sd_run:
                name, total, sent = self.sd_run
                self.sd_run = (name, total, min(total, sent + 1))

    def _finish_probe(self, hit):
        self.probing = False
        self.prb = list(self.mpos)
        self.prb_ok = 1 if hit else 0
        self.target = list(self.mpos)
        if hit:
            self.state = "Idle"
            self.note("probe contact")
        else:
            self.state, self.alarm = "Alarm", 4
            self.note("probe travel exhausted with no contact -> Alarm 4")


# --------------------------------------------------------------------------
# Command handling
# --------------------------------------------------------------------------

GCODE_EXT = (".g", ".gc", ".gco", ".gcode", ".ngc", ".nc", ".txt", ".cnc", ".tap")

# The bench's stand-in filesystems. localfs holds the same non-gcode files the
# real Corgi has, which is the point: $Files/ListGCode filters them all out.
LOCALFS = [("config.yaml", 6815), ("favicon.ico", 1150), ("index.html.gz", 107835)]
SDDIR = os.path.join(HERE, "sdcard")
# Real files where sim/sdcard has them, so $XS can send actual G-code; the
# listed size is then the true size. logo.svg stays a name only.
SDFS = [(n, os.path.getsize(os.path.join(SDDIR, n)) if os.path.exists(os.path.join(SDDIR, n)) else sz)
        for n, sz in [("Merged Toolpath 1.tap", 82360), ("bracket.nc", 14022),
                      ("spoilboard surface.gcode", 5310), ("logo.svg", 2201)]]


class Session:
    """One connected channel. FluidNC reports per-channel, so state like
    'has this channel asked for anything yet' lives here, not on the machine."""

    def __init__(self, m, send):
        self.m = m
        self.send = send
        self.woken = False
        self.limits_monitor = False
        self.buf = ""
        self.xm = None          # queue of raw bytes while an XModem send owns the link

    def out(self, s):
        self.send(s + "\r\n")

    def feed(self, data):
        for ch in data:
            # Checked per byte: the receiver's 'C' can arrive in the same read
            # as the $XS line that started the transfer.
            if self.xm is not None:
                self.xm.put(ch)
                continue
            c = bytes([ch])
            if c in b"?~!\x18\x84\x85\x11\x13" or 0x90 <= ch <= 0x9D:
                self.realtime(ch)
            elif c in b"\r\n":
                if self.buf.strip():
                    self.line(self.buf.strip())
                self.buf = ""
            else:
                self.buf += c.decode("latin1")
                if len(self.buf) > 512:
                    self.buf = ""

    def realtime(self, ch):
        m = self.m
        self.woken = True
        if ch == ord("?"):
            self.out(m.status())
        elif ch == ord("!"):
            with m.lock:
                # While the limit monitor runs, ! is only its exit -- real
                # FluidNC returns early from the feedhold. page_diag.cpp
                # depends on exactly this.
                if self.limits_monitor:
                    self.limits_monitor = False
                    m.note("limit monitor stopped")
                elif m.state in ("Run", "Jog"):
                    m.state = "Hold"
                    m.note("feed hold")
        elif ch == ord("~"):
            with m.lock:
                if m.state == "Hold":
                    m.state = "Run" if m.sd_run else "Idle"
                    m.note("cycle start")
                elif m.state == "Door" and not m.switches.get("D"):
                    m.state = "Idle"
        elif ch == 0x18:
            with m.lock:
                m._reset_state()
                m.note("soft reset")
            self.out("")
            self.out("Grbl 3.9 [FluidNC v4.0.3 (esp32-wifi) '$' for help]")
        elif ch == 0x85:
            with m.lock:
                if m.state == "Jog":
                    m.target = list(m.mpos)
                    m.state = "Idle"
        elif ch == 0x84:
            with m.lock:
                m.state = "Door"
        elif 0x90 <= ch <= 0x9D:
            self._override(ch)

    def _override(self, ch):
        m = self.m
        with m.lock:
            if ch == 0x90:
                m.ov[0] = 100
            elif ch in (0x91, 0x92, 0x93, 0x94):
                m.ov[0] += {0x91: 10, 0x92: -10, 0x93: 1, 0x94: -1}[ch]
            elif ch == 0x95:
                m.ov[1] = 100
            elif ch == 0x96:
                m.ov[1] = 50
            elif ch == 0x97:
                m.ov[1] = 25
            elif ch == 0x99:
                m.ov[2] = 100
            elif ch in (0x9A, 0x9B, 0x9C, 0x9D):
                m.ov[2] += {0x9A: 10, 0x9B: -10, 0x9C: 1, 0x9D: -1}[ch]
            m.ov[0] = max(10, min(200, m.ov[0]))
            m.ov[2] = max(10, min(200, m.ov[2]))

    # ---- line commands -----------------------------------------------------

    def line(self, s):
        m = self.m
        self.woken = True
        m.note(f"<- {s}")
        if s.startswith("$"):
            if self.dollar(s):
                return
            self.out("ok")
            return
        # G-code. Motion words move the machine so a running job looks alive.
        self.gcode(s)
        self.out("ok")

    def dollar(self, s):
        """Returns True when the handler already emitted its own terminator."""
        m = self.m
        body = s[1:]
        up = body.upper()

        if body == "" or up == "HELP":
            self.out("[MSG: Commands: $$ $# $S $CD $H $X $J= $Files/ListGCode $SD/List]")
            return False

        if up.startswith("XS="):
            return self.xmodem_send(body[3:])

        if up == "I":
            self.out("[VER:4.0 FluidNC v4.0.3 (esp32-wifi) :]")
            self.out("[OPT:PHS]")
            self.out(f"[MSG:Machine: {m.cfg.get('name', 'sim')}]")
            self.out("[MSG:Mode=STA:SSID=YAX-Net:Status=Connected:IP=127.0.0.1:MAC=SIM]")
            return False

        if up in ("GD", "GPIO/DUMP"):
            # FluidNC's gpio_dump: "<n> GPIO<n>[ I<level>]" per pin, then the
            # input matrix. Levels come from the config's input pins: 1 idle
            # (they are active-low), 0 while the bench switch is pressed.
            pins = {}
            def walk(n, path=""):
                if isinstance(n, dict):
                    for k, v in n.items(): walk(v, f"{path}/{k}")
                elif isinstance(n, str) and n.startswith("gpio."):
                    num = int(re.match(r"gpio\.(\d+)", n).group(1))
                    pins[num] = path
            walk(m.cfg)
            for g in [0, 1, 2, 3, 4, 5] + list(range(12, 20)) + [21, 22, 23, 25, 26, 27] + list(range(32, 40)):
                path = pins.get(g, "")
                if "limit" in path or "probe" in path or "control" in path:
                    self.out(f"{g} GPIO{g} I1")
                else:
                    self.out(f"{g} GPIO{g}")
            self.out("Input Matrix")
            return False

        if up in ("CI", "CHANNEL/INFO"):
            for name in ("USB", "uart_channel2"):
                self.out(f"[MSG:INFO: {name}]")
            return False

        if up in ("S", "SC", "$"):
            for l in m.settings_lines():
                self.out(l)
            return False

        if "=" not in body:
            # $<name> reads one setting back, as FluidNC does: "$name=value".
            for l in m.settings_lines():
                if l.lower().startswith("$" + body.lower() + "="):
                    self.out(l)
                    return False

        if up.startswith(("CD=", "CONFIG/DUMP=")):
            # $CD=<file>: FluidNC writes the live config tree over <file>.
            # Nothing to write here; acknowledge like the real thing.
            m.note(f"config saved to {body.split('=', 1)[1]}")
            return False

        if up in ("CD", "CONFIG/DUMP"):
            for l in yaml.safe_dump(m.cfg, sort_keys=False, default_flow_style=False).splitlines():
                self.out(l)
            return False

        if up == "G":
            self.out(f"[GC:{m.wcs} G0 G17 G21 G90 G94 M5 M9 T0 F0 S0]")
            return False

        if up == "#":
            n = m.n_axes
            for k in ("G54", "G55", "G56", "G57", "G58", "G59"):
                v = ",".join(f"{x:.3f}" for x in m.offsets[k])
                self.out(f"[{k}:{v}]")
            z = ",".join("0.000" for _ in range(n))
            for k in ("G59.1", "G59.2", "G59.3", "G28", "G30"):
                self.out(f"[{k}:{z}]")
            self.out(f"[G92:{','.join(f'{x:.3f}' for x in m.g92)}]")
            self.out(f"[TLO:{','.join(f'{m.tlo:.3f}' for _ in range(n))}]")
            self.out(f"[PRB:{','.join(f'{x:.3f}' for x in m.prb)}:{m.prb_ok}]")
            return False

        # $H, or $HX / $HZ for one axis -- but NOT $Hostname=..., which a bare
        # startswith("H") would otherwise swallow into the homing path.
        if re.fullmatch(r"H[XYZABC]*", up):
            return self.home(body[1:])

        if up == "X":
            with m.lock:
                if m.state == "Alarm":
                    m.state, m.alarm = "Idle", 0
                    m.note("unlocked")
                    self.out("[MSG:Caution: Unlocked]")
            return False

        if up.startswith("J="):
            return self.jog(body[2:])

        if up.startswith("LIMITS/SHOW"):
            return self.limits_show()

        if up.startswith("FILES/LISTGCODE"):
            return self.list_gcode(body)

        if up.startswith("SD/LIST"):
            self.out_files(SDFS, "/sd", "7.39 GB", "4.00 KB", "7.39 GB")
            return False

        if up.startswith("LOCALFS/LIST"):
            self.out_files(LOCALFS, "/littlefs", "64.00 KB", "128.00 KB", "192.00 KB")
            return False

        if up.startswith("SD/RUN="):
            return self.sd_run(body[7:])

        if up == "BYE":
            self.out("[MSG:Restarting]")
            return False

        if "=" in body:
            name, _, value = body.partition("=")
            err = m.write_setting(name, value)
            if err:
                self.out(err)
                self.out("[MSG:ERR: Invalid $ statement]")
                return True
            return False

        self.out("error:3")
        self.out("[MSG:ERR: Invalid $ statement]")
        return True

    def home(self, axes):
        m = self.m
        with m.lock:
            if not m.caps["homing"]:
                # Grbl error 5. This is the exact reply the bring-up Corgi
                # gives, and the reason the panel must grey the button out
                # instead of offering it and failing.
                self.out("error:5")
                self.out("[MSG:ERR: Homing cycle is not configured]")
                return True
            want = [a for a in m.axis_names if a in axes.upper()] or m.axis_names
            want = [a for a in want if m.axis[a]["homing_cycle"] >= 0]
            if not want:
                self.out("error:5")
                self.out("[MSG:ERR: That axis has no homing cycle]")
                return True
            m.state = "Home"
            for a in want:
                i = m.axis_names.index(a)
                m.target[i] = 0.0
                m.mpos[i] = m.mpos[i] or 25.0     # somewhere to travel from
            m.note(f"homing {''.join(want)}")
        return False

    def jog(self, spec):
        m = self.m
        with m.lock:
            if m.state == "Alarm":
                self.out("error:9")
                return True
            rel = "G91" in spec.upper()
            words = dict(re.findall(r"([A-Za-z])(-?\d+\.?\d*)", spec))
            moved = False
            for i, a in enumerate(m.axis_names):
                for key in (a, a.lower()):
                    if key in words:
                        v = float(words[key])
                        m.target[i] = (m.mpos[i] + v) if rel else v
                        moved = True
                        break
            if moved:
                m.state = "Jog"
        return False

    def limits_show(self):
        m = self.m
        if not m.limit_lamps:
            self.out("[MSG:INFO: No limit pins are configured]")
            return False
        self.limits_monitor = True
        # The preamble page_diag.cpp parses: lowercase = motor0, upper = motor1.
        letters = "".join(l[0].lower() if len(l) == 1 else l[0].upper() for l in m.limit_lamps)
        self.out(f"Limit Axes :{letters}")
        self.out("Send ! to exit")
        m.note("limit monitor started")
        return False

    def list_gcode(self, body):
        m = self.m
        path = body.partition("=")[2] or ""
        files = SDFS if (path.startswith("/sd") or not path) else LOCALFS
        if path.startswith("/localfs") or path.startswith("/littlefs"):
            files = LOCALFS
        keep = [(n, s) for n, s in files if n.lower().endswith(GCODE_EXT)]
        blob = ",".join('{"name":"%s","size":"%d"}' % (n, s) for n, s in keep)
        self.out('{"files":[%s],"path":"%s"}' % (blob, path))
        return False

    def out_files(self, files, mount, free, used, total):
        for n, s in files:
            self.out(f"[FILE: {n}|SIZE:{s}]")
        self.out(f"[{mount} Free:{free} Used:{used} Total:{total}]")

    def xmodem_send(self, path):
        """$XS: FluidNC's xmodemTransmit. Waits for the receiver's 'C', sends
        1K CRC blocks, then EOT, then the command's ok. Runs on its own thread
        so the link handler keeps feeding ACKs into the queue."""
        name = path.split("/")[-1]
        f = os.path.join(SDDIR, name)
        if not os.path.exists(f):
            self.out("error:65")          # FsFileNotFound
            return True
        data = open(f, "rb").read()
        self.xm = queue.Queue()

        def crc16(b):
            c = 0
            for x in b:
                c ^= x << 8
                for _ in range(8):
                    c = ((c << 1) ^ 0x1021) if c & 0x8000 else c << 1
                    c &= 0xFFFF
            return c

        def get(t):
            try:
                return self.xm.get(timeout=t)
            except queue.Empty:
                return None

        def run():
            ok = False
            try:
                for _ in range(16):              # 16 x 2 s, as FluidNC
                    if get(2.0) == ord("C"):
                        break
                else:
                    return
                blocks = [data[i:i + 1024] for i in range(0, len(data), 1024)] or [b""]
                for n, blk in enumerate(blocks, 1):
                    blk = blk.ljust(1024, b"\x1a")
                    pkt = bytes([0x02, n & 0xFF, 0xFF - (n & 0xFF)]) + blk + crc16(blk).to_bytes(2, "big")
                    for _ in range(10):
                        self.send(pkt.decode("latin1"))
                        if get(2.0) == 0x06:
                            break
                    else:
                        return
                for _ in range(10):
                    self.send("\x04")
                    if get(2.0) == 0x06:
                        ok = True
                        break
            finally:
                self.xm = None
                m.note(f"$XS {name}: {'sent' if ok else 'failed'} {len(data)} bytes")
                self.out("ok" if ok else "error:66")

        m = self.m
        threading.Thread(target=run, daemon=True).start()
        return True

    def sd_run(self, path):
        m = self.m
        name = path.split("/")[-1]
        known = dict(SDFS)
        if name not in known:
            self.out("error:33")
            self.out(f"[MSG:ERR: No such file {path}]")
            return True
        with m.lock:
            if m.caps["must_home"] and not m.homed:
                self.out("error:9")
                self.out("[MSG:ERR: Must home first]")
                return True
            m.sd_run = (path, 400, 0)
            m.state = "Run"
            m.spindle_on = True
            m.spindle_rpm = 12000
            # Give the job somewhere to travel so the DRO and progress move.
            for i, a in enumerate(m.axis_names[:3]):
                m.target[i] = m.axis[a]["max_travel"] * 0.25
            m.note(f"running {path}")
        return False

    def gcode(self, s):
        m = self.m
        up = s.upper()
        with m.lock:
            # Driver reset relays. M64/M65 P<n> addresses the user_outputs
            # SLOT (FluidNC GCode.cpp:1748), and slots 0-4 map to the five
            # driver relays (X Y1 Y2 Z A) per the 2026-09-07 contract. M64
            # energizes the NC relay = driver power CUT; M65 restores it,
            # which also clears the simulated fault.
            pm = re.match(r"M6([45])\s*P(\d+)$", up.strip())
            if pm:
                out = int(pm.group(2))
                if 0 <= out <= 4 and str(out) in m.switches:
                    on = pm.group(1) == "5"
                    m.switches[str(out)] = on
                    m.note(f"driver {out} power {'restored' if on else 'CUT'}")
                return None
            if "M3" in up or "M4" in up:
                m.spindle_on = True
                sm = re.search(r"S(\d+)", up)
                m.spindle_rpm = int(sm.group(1)) if sm else 10000
            if "M5" in up:
                m.spindle_on, m.spindle_rpm = False, 0
            if "M8" in up and m.caps["flood"]:
                m.flood = True
            if "M7" in up and m.caps["mist"]:
                m.mist = True
            if "M9" in up:
                m.flood = m.mist = False
            if "G38" in up:
                if not m.caps["probe"]:
                    m.note("G38 with no probe pin configured")
                m.probing = True
                m.state = "Run"
            rel = "G91" in up
            words = dict(re.findall(r"([XYZABC])(-?\d+\.?\d*)", up))
            for i, a in enumerate(m.axis_names):
                if a in words:
                    v = float(words[a])
                    m.target[i] = (m.mpos[i] + v) if rel else v
                    if m.state == "Idle":
                        m.state = "Run"


# --------------------------------------------------------------------------
# Servers
# --------------------------------------------------------------------------

class LinkServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def make_handler(m, sessions):
    class H(socketserver.BaseRequestHandler):
        def handle(self):
            sock = self.request
            sock.settimeout(0.05)
            lock = threading.Lock()

            def send(s):
                with lock:
                    try:
                        sock.sendall(s.encode("latin1"))
                    except OSError:
                        pass

            sess = Session(m, send)
            sessions.append(sess)
            m.note(f"link connected from {self.client_address[0]}")
            next_report = 0.0
            next_sample = 0.0
            try:
                while True:
                    try:
                        data = sock.recv(1024)
                        if not data:
                            break
                        sess.feed(data)
                    except socket.timeout:
                        pass
                    except OSError:
                        break
                    now = time.time()
                    # FluidNC only auto-reports on a channel once that channel
                    # has sent something. The panel's boot XON is what does it.
                    if sess.woken and now >= next_report:
                        next_report = now + m.report_ms / 1000.0
                        send(m.status() + "\r\n")
                    if sess.limits_monitor and now >= next_sample:
                        next_sample = now + 0.5
                        act = "".join(l[0].lower() for l in m.limit_lamps if m.switches.get(l))
                        probe = "P" if m.switches.get("P") else ""
                        send(f": {act}  {probe}\r\n")
            finally:
                sessions.remove(sess)
                m.note("link disconnected")
    return H


PANEL = os.path.join(HERE, "panel.html")


def make_http(m, sessions):
    class H(BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def _json(self, obj, code=200):
            b = json.dumps(obj).encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(b)))
            self.end_headers()
            self.wfile.write(b)

        def do_GET(self):
            # Live panel frame, proxied from the LVGL sim's web previewer so
            # the page stays one origin — the bench is ONE app in ONE tab.
            if self.path.startswith("/panel/frame"):
                try:
                    with urllib.request.urlopen(
                            f"http://127.0.0.1:{PANEL_WEB}/frame.bmp",
                            timeout=1.0) as r:
                        b = r.read()
                except OSError:
                    return self._json({"error": "panel offline"}, 503)
                self.send_response(200)
                self.send_header("Content-Type", "image/bmp")
                self.send_header("Content-Length", str(len(b)))
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(b)
                return
            if self.path.startswith("/api/state"):
                with m.lock:
                    return self._json({
                        "profile": m.profile,
                        "machine": m.cfg.get("name", ""),
                        "state": m.state,
                        "alarm": m.alarm,
                        "axes": m.axis_names,
                        "mpos": [round(v, 3) for v in m.mpos],
                        "wpos": [round(m.mpos[i] - m.wco[i], 3) for i in range(m.n_axes)],
                        "caps": m.caps,
                        "lamps": m.limit_lamps,
                        "switches": m.switches,
                        "spindle": {"on": m.spindle_on, "rpm": m.spindle_rpm},
                        "flood": m.flood, "mist": m.mist,
                        "homed": m.homed, "ov": m.ov,
                        "links": len(sessions),
                        "job": m.sd_run,
                        "axis_cfg": {a: m.axis[a] for a in m.axis_names},
                        "pins": m.pin_map,
                        "log": m.log[-40:],
                    })
            body = open(PANEL, "rb").read()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_POST(self):
            # Touch passthrough carries its payload in the query string, no
            # body — forward before the JSON parse below eats the request.
            if self.path.startswith("/panel/touch"):
                try:
                    urllib.request.urlopen(urllib.request.Request(
                        f"http://127.0.0.1:{PANEL_WEB}"
                        + self.path[len("/panel"):], method="POST"),
                        timeout=0.5).read()
                except OSError:
                    pass
                return self._json({"ok": True})
            n = int(self.headers.get("Content-Length", 0))
            try:
                req = json.loads(self.rfile.read(n) or b"{}")
            except ValueError:
                return self._json({"error": "bad json"}, 400)

            if self.path == "/api/switch":
                with m.lock:
                    name = req.get("name")
                    if name not in m.switches:
                        return self._json({"error": "no such switch"}, 404)
                    m.switches[name] = bool(req.get("value"))
                    m.note(f"switch {name} = {'MADE' if m.switches[name] else 'open'}")
                return self._json({"ok": True})

            if self.path == "/api/profile":
                load_profile(m, req.get("name", "bringup"))
                return self._json({"ok": True, "profile": m.profile})

            if self.path == "/api/reset":
                with m.lock:
                    m._reset_state()
                    m.note("bench reset")
                return self._json({"ok": True})

            if self.path == "/api/cmd":
                line = (req.get("line") or "").strip()
                if line and sessions:
                    sessions[0].line(line)
                return self._json({"ok": True})

            return self._json({"error": "not found"}, 404)
    return H


def load_profile(m, name):
    base = yaml.safe_load(open(os.path.join(PROFILES, "corgi_bringup.yaml")))
    overlay = None
    if name == "equipped":
        overlay = yaml.safe_load(open(os.path.join(PROFILES, "equipped.overlay.yaml")))
    with m.lock:
        m.profile = name
        m.cfg = deep_merge(base, overlay) if overlay else base
        m._derive()
        m._reset_state()
        m.note(f"loaded profile '{name}'")


PANEL_WEB = 8081     # where the spawned LVGL sim serves frames/touches


def spawn_panel(link_port, windowed):
    """The LVGL sim as a child process: one command brings up the whole
    bench, one Ctrl-C tears it all down. Headless by default — the panel
    lives in the browser page; --windowed also opens the SDL window."""
    exe = os.path.join(HERE, "cncsim")
    if not os.access(exe, os.X_OK):
        print("panel: sim/cncsim not built (make -C sim) — bench only")
        return
    time.sleep(0.3)   # let the link listener win the race; the panel does
                      # retry, but booting already-connected primes cleanly
    env = dict(os.environ)
    if not windowed:
        env["SDL_VIDEODRIVER"] = "dummy"
    proc = subprocess.Popen(
        [exe, "--port", str(link_port),
         "--sd", os.path.join(HERE, "sdcard"),
         "--web", str(PANEL_WEB)],
        env=env, cwd=HERE,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    atexit.register(proc.terminate)


def main():
    global PANEL_WEB
    ap = argparse.ArgumentParser()
    ap.add_argument("--profile", default="bringup", choices=["bringup", "equipped"])
    ap.add_argument("--link-port", type=int, default=2323)
    ap.add_argument("--web-port", type=int, default=8080)
    ap.add_argument("--panel-web", type=int, default=PANEL_WEB)
    ap.add_argument("--no-panel", action="store_true",
                    help="serve the bench only; don't spawn the LVGL sim")
    ap.add_argument("--windowed", action="store_true",
                    help="panel also opens its SDL window")
    args = ap.parse_args()
    PANEL_WEB = args.panel_web

    base = yaml.safe_load(open(os.path.join(PROFILES, "corgi_bringup.yaml")))
    m = Machine(base, profile=args.profile)
    if args.profile == "equipped":
        load_profile(m, "equipped")

    sessions = []
    link = LinkServer(("0.0.0.0", args.link_port), make_handler(m, sessions))
    threading.Thread(target=link.serve_forever, daemon=True).start()
    web = HTTPServer(("0.0.0.0", args.web_port), make_http(m, sessions))
    threading.Thread(target=web.serve_forever, daemon=True).start()

    if not args.no_panel:
        spawn_panel(args.link_port, args.windowed)

    print(f"FluidNC v4.0.3 sim  profile={m.profile}  ({m.cfg.get('name','')})")
    print(f"  link : tcp://127.0.0.1:{args.link_port}")
    print(f"  bench: http://127.0.0.1:{args.web_port}   (panel embedded)")
    print(f"  caps : {json.dumps(m.caps)}")
    try:
        while True:
            m.tick()
            time.sleep(0.02)
    except KeyboardInterrupt:
        print("\nbye")


if __name__ == "__main__":
    main()
