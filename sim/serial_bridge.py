#!/usr/bin/env python3
"""Bridge the physical panel's USB serial port to fluidnc_sim.py.

    python3 sim/serial_bridge.py [/dev/ttyUSB0] [127.0.0.1:2323]

The panel is the real firmware on real glass; fluidnc_sim.py is the controller.
socat did not carry the replies back to the panel at 1 Mbaud (the panel sat on
NO LINK while the sim saw its polls), so this does it with pyserial, the same
library the direct tests use. Reconnects on its own: a panel reset or a USB
drop-out just means the port vanishes for a few seconds.
"""
import socket
import sys
import threading
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyUSB0"
HOST, TCP = (sys.argv[2] if len(sys.argv) > 2 else "127.0.0.1:2323").rsplit(":", 1)
BAUD = 1000000   # FNC_BAUD in platformio.ini


def pump(src_read, dst_write, stop):
    try:
        while not stop.is_set():
            data = src_read()
            if data:
                dst_write(data)
    except (OSError, serial.SerialException):
        pass
    stop.set()


while True:
    try:
        ser = serial.Serial(PORT, BAUD, timeout=0.05)
        sock = socket.create_connection((HOST, int(TCP)))
        sock.settimeout(0.05)
    except (OSError, serial.SerialException):
        time.sleep(1)
        continue

    def sock_read():
        try:
            d = sock.recv(4096)
        except socket.timeout:
            return b""
        if not d:
            raise OSError("sim closed")
        return d

    print(f"bridging {PORT} @ {BAUD} <-> {HOST}:{TCP}", flush=True)
    stop = threading.Event()
    threads = [threading.Thread(target=pump, args=(lambda: ser.read(4096), sock.sendall, stop), daemon=True),
               threading.Thread(target=pump, args=(sock_read, ser.write, stop), daemon=True)]
    for t in threads:
        t.start()
    stop.wait()
    for c in (ser, sock):
        try:
            c.close()
        except Exception:
            pass
    print("link dropped; retrying", flush=True)
    time.sleep(1)
