#!/usr/bin/env python3
import json
import math
import os
import socket
import struct
import subprocess
import time
import wave

SOCK = "/run/user/1000/fxsound-linux/control.sock"

def cmd(command: str):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(SOCK)
    s.sendall((command + "\n").encode())
    s.shutdown(socket.SHUT_WR)
    data = b""
    while True:
        chunk = s.recv(65535)
        if not chunk:
            break
        data += chunk
    s.close()
    return json.loads(data.decode())

initial_status = cmd("STATUS")
original_preset = initial_status.get("preset", "General")
presets = cmd("PRESETS")["presets"]
wav_path = "/tmp/fxsound-factory-test.wav"
sample_rate = 48000

with wave.open(wav_path, "wb") as w:
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(sample_rate)
    for n in range(int(sample_rate * 0.18)):
        value = (
            0.008 * math.sin(2 * math.pi * 110 * n / sample_rate)
            + 0.005 * math.sin(2 * math.pi * 3500 * n / sample_rate)
        )
        q = max(-32768, min(32767, int(value * 32767)))
        w.writeframesraw(struct.pack("<hh", q, q))

failures = []
print("preset|bands|nonzero_eq|input_peak|output_peak|finite")

try:
    for name in presets:
        loaded = cmd("PRESET " + name)
        if not loaded.get("ok"):
            failures.append((name, "load"))
            continue

        cmd("METERS")
        subprocess.run(
            ["pw-play", "--target", "fxsound_sink", wav_path],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=True,
        )
        time.sleep(0.08)

        status = cmd("STATUS")
        meters = cmd("METERS")
        nonzero = sum(abs(float(item["g"])) > 1e-6 for item in status["eq"])
        input_peak = float(meters["input"])
        output_peak = float(meters["output"])
        finite = math.isfinite(input_peak) and math.isfinite(output_peak)

        print(
            f"{name}|{status['bands']}|{nonzero}|"
            f"{input_peak:.6f}|{output_peak:.6f}|{finite}"
        )

        if (
            status["bands"] not in (5, 10, 15, 20, 31)
            or not finite
            or input_peak <= 0.0
            or output_peak <= 0.0
        ):
            failures.append((name, "audio"))
finally:
    if original_preset in presets:
        cmd("PRESET " + original_preset)
    try:
        os.remove(wav_path)
    except FileNotFoundError:
        pass

if failures:
    print("FAIL", failures)
    raise SystemExit(1)

print("ALL_FACTORY_PRESETS_OK")
