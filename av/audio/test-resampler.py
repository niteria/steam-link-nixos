#!/usr/bin/env python3
"""Play 44.1 kHz tones through pcm-sink -o 48000 into the mock SDK.

Checks the 48 kHz stream geometry, the exact 160/147 output length, and for a
1 kHz and a 15 kHz tone the fitted gain and the residual (noise, distortion,
images) on both channels."""
import array
import math
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

sink, work = sys.argv[1], Path(sys.argv[2])
fifo, journal, pcm = work / "pcm", work / "journal", work / "pcm.out"
for path in (fifo, journal, pcm):
    path.unlink(missing_ok=True)
os.mkfifo(fifo, 0o600)
process = subprocess.Popen(
    [sink, "-o", "48000", "44100", str(fifo)],
    env=dict(os.environ, MOCK_JOURNAL=str(journal), MOCK_PCM=str(pcm)),
)
tones = [(1000, 16384), (15000, 16384)]
samples = array.array("h")
for frequency, amplitude in tones:
    for n in range(44100):
        value = amplitude * math.sin(2 * math.pi * frequency * n / 44100)
        samples.extend((round(value), round(-value / 2)))
data = samples.tobytes()
with open(fifo, "wb") as writer:
    writer.write(data)
deadline = time.monotonic() + 15
while time.monotonic() < deadline and pcm.stat().st_size < 4 * (96000 + 2 * 2400):
    time.sleep(0.1)
process.send_signal(signal.SIGTERM)
assert process.wait(timeout=10) == 0, process.returncode
assert "create 48000 2 9600" in journal.read_text(), journal.read_text()
output = array.array("h", pcm.read_bytes())
chunks = -(-88200 // 2205)
assert len(output) == 2 * 2400 * (chunks + 2), len(output)


def fit(channel, start, frequency, count):
    w = 2 * math.pi * frequency / 48000
    ys = [output[2 * (start + n) + channel] for n in range(count)]
    a = 2 / count * sum(y * math.cos(w * (start + n)) for n, y in enumerate(ys))
    b = 2 / count * sum(y * math.sin(w * (start + n)) for n, y in enumerate(ys))
    residual = sum((y - a * math.cos(w * (start + n)) - b * math.sin(w * (start + n))) ** 2
                   for n, y in enumerate(ys)) / count
    return math.hypot(a, b), 10 * math.log10(residual / (math.hypot(a, b) ** 2 / 2))


for index, (frequency, amplitude) in enumerate(tones):
    start = 48000 * index + 4800
    for channel, expected in ((0, amplitude), (1, amplitude / 2)):
        gain, residual_db = fit(channel, start, frequency, 38400)
        gain_db = 20 * math.log10(gain / expected)
        print("resampler %5d Hz ch%d: gain %+.3f dB, residual %.1f dB" % (frequency, channel, gain_db, residual_db))
        assert abs(gain_db) < 0.1, gain_db
        assert residual_db < -70, residual_db
