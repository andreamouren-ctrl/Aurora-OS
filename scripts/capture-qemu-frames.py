#!/usr/bin/env python3
import os
import socket
import time
from pathlib import Path

MONITOR = "/tmp/aurora-monitor.sock"
CAPTURE = Path("build/capture").resolve()
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
    data = b""
    while True:
        try:
            chunk = sock.recv(65536)
            if not chunk:
                return data
            data += chunk
        except (socket.timeout, BlockingIOError):
            return data


def hmp(command, settle=0.04):
    sock.sendall((command + "\n").encode("ascii"))
    time.sleep(settle)
    return drain()


def dump(path):
    path = Path(path).resolve()
    response = hmp(f"screendump {path}", 0.05)
    deadline = time.time() + 1.0
    while not path.exists() and time.time() < deadline:
        time.sleep(0.02)
    if not path.exists():
        raise SystemExit(
            f"QEMU did not create screendump {path}: {response.decode(errors='replace')}"
        )


time.sleep(0.05)
drain()
marker_frame = None
post_marker = 0

for i in range(140):
    dump(CAPTURE / f"frame_{i:03d}.ppm")

    serial = SERIAL.read_text(errors="replace") if SERIAL.exists() else ""
    if SUCCESS in serial and marker_frame is None:
        marker_frame = i
        dump(CAPTURE / "login.ppm")

    if marker_frame is not None:
        post_marker += 1
        if post_marker >= 16:
            break

    time.sleep(0.16)

if marker_frame is None:
    hmp("quit")
    raise SystemExit("Aurora success marker was not reached")

dump(CAPTURE / "final.ppm")
hmp("quit")
sock.close()

(CAPTURE / "capture-meta.txt").write_text(
    f"success_marker_frame={marker_frame}\n"
    f"frames_after_marker={post_marker}\n"
)
