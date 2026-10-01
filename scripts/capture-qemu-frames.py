#!/usr/bin/env python3
import os
import socket
import time
from pathlib import Path

MONITOR = "/tmp/aurora-monitor.sock"
CAPTURE = Path("build/capture")
SERIAL = CAPTURE / "serial.log"
SUCCESS = "[kernel] M1 user-space bootstrap reached successfully"

CAPTURE.mkdir(parents=True, exist_ok=True)

deadline = time.time() + 10.0
while not os.path.exists(MONITOR):
    if time.time() >= deadline:
        raise SystemExit("QEMU monitor socket did not appear")
    time.sleep(0.05)

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect(MONITOR)
sock.settimeout(0.15)


def drain():
    while True:
        try:
            if not sock.recv(65536):
                return
        except (socket.timeout, BlockingIOError):
            return


def hmp(command, settle=0.04):
    sock.sendall((command + "\n").encode("ascii"))
    time.sleep(settle)
    drain()


time.sleep(0.05)
drain()
marker_frame = None
post_marker = 0

for i in range(140):
    hmp(f"screendump {CAPTURE / f'frame_{i:03d}.ppm'}", 0.025)

    serial = SERIAL.read_text(errors="replace") if SERIAL.exists() else ""
    if SUCCESS in serial and marker_frame is None:
        marker_frame = i
        hmp(f"screendump {CAPTURE / 'login.ppm'}", 0.05)

    if marker_frame is not None:
        post_marker += 1
        if post_marker >= 16:
            break

    time.sleep(0.16)

if marker_frame is None:
    hmp("quit")
    raise SystemExit("Aurora success marker was not reached")

hmp(f"screendump {CAPTURE / 'final.ppm'}", 0.05)
hmp("quit")
sock.close()

(CAPTURE / "capture-meta.txt").write_text(
    f"success_marker_frame={marker_frame}\n"
    f"frames_after_marker={post_marker}\n"
)
