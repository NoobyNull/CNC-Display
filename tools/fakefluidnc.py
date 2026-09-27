#!/usr/bin/env python3
"""Minimal FluidNC stand-in on the FTDI, so the panel has something to talk to.

Answers the panel's '?' status polls with a 4-axis Grbl report. That is enough
for transport_poll() to see state != Disconnected, stop the baud hunt, and for
the DRO to learn n_axes = 4 -- which is the only way to exercise the A-axis
column without the machine wired.

Not an emulator. It answers the handful of things the panel asks at startup and
echoes 'ok' to everything else so send_line() does not block on _ackwait.
"""
import serial, sys, time

PORT = sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyUSB0'
BAUD = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
SECS = int(sys.argv[3]) if len(sys.argv) > 3 else 30

# X, Y, Z, A. Non-zero and distinct so a transposed axis is obvious on screen.
MPOS = [10.000, 20.000, 5.000, 45.000]
WCO  = [1.000, 2.000, 3.000, 0.000]

s = serial.Serial(PORT, BAUD, timeout=0.05)
s.reset_input_buffer()

def status():
    m = ",".join(f"{v:.3f}" for v in MPOS)
    w = ",".join(f"{v:.3f}" for v in WCO)
    return f"<Idle|MPos:{m}|FS:0,0|WCO:{w}>\r\n".encode()

line = b""
polls = 0
sent  = 0
end = time.time() + SECS
next_auto = 0.0
print(f"answering on {PORT} @ {BAUD} for {SECS}s")

while time.time() < end:
    # The panel asks for $RI=200, i.e. push a report every 200ms rather than
    # polling. Without these it decides the link went away.
    now = time.time()
    if now >= next_auto:
        next_auto = now + 0.2
        s.write(status()); s.flush()
        sent += 1

    b = s.read(64)
    if not b:
        continue
    for ch in b:
        c = bytes([ch])
        if c == b'?':                      # realtime status request
            polls += 1
            s.write(status()); s.flush()
            sent += 1
        elif c in (b'\r', b'\n'):
            if line:
                txt = line.decode('ascii', 'replace').strip()
                print(f"  <- {txt!r}")
                if txt == '$#':
                    for g in ("G54:1.000,2.000,3.000,0.000",
                              "G55:0.000,0.000,0.000,0.000",
                              "TLO:0.000", "PRB:0.000,0.000,0.000,0.000:1"):
                        s.write(f"[{g}]\r\n".encode())
                s.write(b"ok\r\n")         # never leave send_line() spinning
                s.flush()
                line = b""
        elif c in (b'\x11', b'\x13'):      # XON/XOFF, ignore
            pass
        else:
            line += c
            if len(line) > 200:
                line = b""

s.close()
print(f"status polls answered: {polls}/{sent}")
