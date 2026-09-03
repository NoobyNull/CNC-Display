#!/usr/bin/env python3
"""Self-check for the FluidNC stand-in. `python3 sim/test_sim.py`

Boots the sim in-process, connects a client to the link port and asserts the
replies the panel actually depends on. The assertions are pinned to the capture
taken from the live Corgi on 2026-08-25, so if this file passes, the sim is
still telling the panel the truth.
"""
import json
import socket
import threading
import time
import urllib.request

import yaml

import fluidnc_sim as S

LINK_PORT, WEB_PORT = 24230, 24231


def boot(profile="bringup"):
    base = yaml.safe_load(open(S.os.path.join(S.PROFILES, "corgi_bringup.yaml")))
    m = S.Machine(base, profile=profile)
    if profile == "equipped":
        S.load_profile(m, "equipped")
    sessions = []
    link = S.LinkServer(("127.0.0.1", LINK_PORT), S.make_handler(m, sessions))
    threading.Thread(target=link.serve_forever, daemon=True).start()
    web = S.HTTPServer(("127.0.0.1", WEB_PORT), S.make_http(m, sessions))
    threading.Thread(target=web.serve_forever, daemon=True).start()

    stop = threading.Event()

    def spin():
        while not stop.is_set():
            m.tick()
            time.sleep(0.02)

    threading.Thread(target=spin, daemon=True).start()
    return m, link, web, stop


class Client:
    def __init__(self):
        self.s = socket.create_connection(("127.0.0.1", LINK_PORT), timeout=3)
        self.s.settimeout(0.25)
        self.buf = ""

    def ask(self, cmd, secs=1.2, keep_status=False):
        self.s.sendall(cmd.encode() + (b"" if cmd in ("?", "!", "~") else b"\n"))
        return self.read(secs, keep_status)

    def read(self, secs=1.2, keep_status=False):
        out, end = [], time.time() + secs
        while time.time() < end:
            try:
                b = self.s.recv(8192)
            except socket.timeout:
                continue
            if not b:
                break
            self.buf += b.decode("latin1")
            while "\n" in self.buf:
                line, self.buf = self.buf.split("\n", 1)
                line = line.strip()
                if line and (keep_status or not line.startswith("<")):
                    out.append(line)
        return out


def api(path, body=None):
    url = f"http://127.0.0.1:{WEB_PORT}{path}"
    if body is None:
        return json.loads(urllib.request.urlopen(url, timeout=3).read())
    req = urllib.request.Request(url, data=json.dumps(body).encode(),
                                 headers={"Content-Type": "application/json"})
    return json.loads(urllib.request.urlopen(req, timeout=3).read())


def main():
    m, link, web, stop = boot("bringup")
    c = Client()
    c.ask("?", 0.5, keep_status=True)

    # --- identity and settings, pinned to the live capture --------------
    ident = c.ask("$I")
    assert any("FluidNC v4.0.3" in l for l in ident), ident

    s = c.ask("$S", 2.0)
    assert "$Grbl/HomingCycleEnable=0" in s, "bring-up must report homing disabled"
    assert "$Grbl/HardLimitsEnable=0" in s
    assert "$Grbl/Resolution/X=39.895" in s, "steps/mm must come from the real config"
    assert "$Grbl/Acceleration/A=200.000" in s
    assert "$Grbl/MaxRate/A=10000.000" in s
    assert not any(l.startswith("$/") or "/probe/pin" in l for l in s), \
        "v4 $S must NOT expose the config tree -- that is the whole bug"
    assert "ok" in s

    # --- homing is refused, which is why the UI must grey it out --------
    h = c.ask("$H")
    assert "error:5" in h, h

    # --- the file list: the reply shape the panel parses ----------------
    f = c.ask("$Files/ListGCode=/sd")
    blob = next(l for l in f if l.startswith("{"))
    doc = json.loads(blob)
    names = [e["name"] for e in doc["files"]]
    assert "Merged Toolpath 1.tap" in names, names
    assert "logo.svg" not in names, "non-gcode must be filtered"
    assert doc["path"] == "/sd"

    # Controller FLASH holds no gcode at all -- an empty list is the correct
    # answer there, not a bug. The panel must say so rather than look broken.
    fl = c.ask("$Files/ListGCode=/localfs")
    doc = json.loads(next(l for l in fl if l.startswith("{")))
    assert doc["files"] == [], doc
    assert any("config.yaml" in l for l in c.ask("$LocalFS/List"))

    # --- settings writes: the accel/speed feature ----------------------
    assert "ok" in c.ask("$Grbl/Acceleration/X=500")
    assert "$Grbl/Acceleration/X=500.000" in c.ask("$S", 2.0)
    bad = c.ask("$Grbl/Acceleration/X=0")
    assert any("error" in l for l in bad), "a zero rate must be rejected"

    # --- work offsets ---------------------------------------------------
    o = c.ask("$#")
    assert any(l.startswith("[G54:") for l in o)
    assert any(l.startswith("[PRB:") for l in o)

    # --- no limit pins configured -> the monitor says so ----------------
    lim = c.ask("$Limits/Show")
    assert any("No limit pins" in l for l in lim), lim

    # --- jog moves the DRO ---------------------------------------------
    before = list(m.mpos)
    c.ask("$J=G91F1000X10")
    time.sleep(1.0)
    assert m.mpos[0] > before[0] + 1.0, (before, m.mpos)

    # --- web bench ------------------------------------------------------
    st = api("/api/state")
    assert st["caps"]["homing"] is False
    assert st["switches"] == {}, "bring-up config declares no input pins"
    assert st["links"] == 1

    # --- equipped profile flips every capability ------------------------
    api("/api/profile", {"name": "equipped"})
    st = api("/api/state")
    assert st["caps"]["homing"] and st["caps"]["probe"] and st["caps"]["flood"]
    assert set(st["switches"]) == {"X", "Y", "Y2", "Z", "A", "P", "T", "D", "E"}, st["switches"]

    c2 = Client()
    c2.ask("?", 0.4, keep_status=True)
    assert "$Grbl/HomingCycleEnable=1" in c2.ask("$S", 2.0)
    hh = c2.ask("$H", 2.0)
    assert not any("error" in l for l in hh), hh

    # A made NPN contact must show up in Pn: on the very next status report.
    api("/api/switch", {"name": "P", "value": True})
    time.sleep(0.3)
    rep = [l for l in c2.ask("?", 0.6, keep_status=True) if l.startswith("<")]
    assert any("Pn:" in l and "P" in l.split("Pn:")[1] for l in rep), rep

    # Limit monitor preamble is what page_diag.cpp parses for lamp existence.
    lim = c2.ask("$Limits/Show", 1.5)
    assert any(l.startswith("Limit Axes :") for l in lim), lim
    c2.ask("!", 0.5)

    # Hard limit during a move must alarm, since this profile enables them.
    # The jog has to still be running when the contact is made, so ask() must
    # not sit reading replies for longer than the move takes: 400mm at the
    # 5000mm/min config rate is ~4.8s of travel against a 0.1s read.
    api("/api/switch", {"name": "P", "value": False})
    c2.ask("$X")
    c2.ask("$J=G91F1000X400", 0.1)
    assert m.state == "Jog", m.state
    api("/api/switch", {"name": "X", "value": True})
    time.sleep(0.5)
    assert m.state == "Alarm" and m.alarm == 1, (m.state, m.alarm)

    stop.set()
    link.shutdown()
    web.shutdown()
    print("all sim checks passed")


if __name__ == "__main__":
    main()
