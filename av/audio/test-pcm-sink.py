#!/usr/bin/env python3
"""Drive a host build of pcm-sink against the mock SDK.

Checks byte-exact delivery across writer sessions, zero padding plus the
silent tail at each session end, stream reuse across short pauses, the idle
stream release, empty writer sessions and an orderly SIGTERM exit."""
import os
import queue
import random
import re
import signal
import subprocess
import sys
import threading
import time
from pathlib import Path

sink, work, rate = sys.argv[1], Path(sys.argv[2]), int(sys.argv[3])
chunk = rate // 20 * 4
tail = 2 * chunk
fifo = work / "pcm"
journal, pcm = work / "journal", work / "pcm.out"
for path in (fifo, journal, pcm):
    path.unlink(missing_ok=True)
os.mkfifo(fifo, 0o600)
lines = queue.Queue()
process = subprocess.Popen(
    [sink, "-v", "-i", "2", str(rate), str(fifo)],
    stderr=subprocess.PIPE, text=True,
    env=dict(os.environ, MOCK_JOURNAL=str(journal), MOCK_PCM=str(pcm)),
)
log = []
threading.Thread(target=lambda: [lines.put(line) for line in process.stderr], daemon=True).start()


def expect(pattern, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            line = lines.get(timeout=0.1)
        except queue.Empty:
            if process.poll() is not None:
                break
            continue
        log.append(line)
        if re.search(pattern, line):
            return line
    sys.exit("missing %r; log:\n%s" % (pattern, "".join(log)))


def session(data):
    # Uneven, frame-misaligned writes, like a decoder's variable packets.
    with open(fifo, "wb", buffering=0) as writer:
        offset = 0
        while offset < len(data):
            step = random.randint(1, 5000)
            writer.write(data[offset:offset + step])
            offset += step
    expect(r"session end frames=", 10)


def padded(data):
    return data + bytes(-len(data) % chunk) + bytes(tail)


random.seed(rate)
expect(r"ready on ", 10)
first = random.randbytes(rate * 4 + 123 * 4 + 2)
second = random.randbytes(rate * 2 + 7 * 4)
third = random.randbytes(rate // 5 * 4)
started = time.monotonic()
session(first)
elapsed = time.monotonic() - started
# The mock consumes in real time and the sink holds at most ~300 ms queued.
assert elapsed > 0.6, "sink did not apply backpressure: %.3f s" % elapsed
session(second)
open(fifo, "wb").close()  # An empty writer session must not open a stream.
expect(r"stream closed \(idle\)", 6)
session(third)
process.send_signal(signal.SIGTERM)
expect(r"stream closed \(stopping\)", 5)
assert process.wait(timeout=5) == 0, process.returncode
expected = padded(first) + padded(second) + padded(third)
actual = pcm.read_bytes()
assert actual == expected, "PCM mismatch: %d vs %d bytes" % (len(actual), len(expected))
events = [line.split()[0] for line in journal.read_text().splitlines()]
assert events == ["context", "create", "free", "create", "free"], events
assert ("create %d 2 %d" % (rate, chunk)) in journal.read_text()
print("pcm-sink %d Hz: %d bytes across 3 sessions, %.2f s first session" % (rate, len(actual), elapsed))
