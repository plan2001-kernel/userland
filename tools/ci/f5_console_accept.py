#!/usr/bin/env python3
"""F5 console test: physical QEMU PS/2 key injection and VGA pixel evidence.

Reads kernel's canonical framebuffer bitmap font; no serial echo assertion,
simulated userland terminal, or fake kernel output. Fails closed on timeouts.
"""
import re
import socket
import sys
import time
from pathlib import Path

from PIL import Image

monitor, serial, out, fontfile = map(Path, sys.argv[1:])
out.mkdir(parents=True, exist_ok=True)

source = fontfile.read_text(encoding="utf-8")
table = source.split("static const ushort fbconsolefont[101][24] = {", 1)[1]
font = []
for row in re.findall(r"\{([^{}]+)\}", table):
    values = re.findall(r"0x[0-9a-fA-F]+", row)
    if len(values) == 24:
        font.append(tuple(int(value, 16) for value in values))
assert len(font) == 101, "unexpected kernel VGA font"
glyphs = {bits: chr(32+i) for i, bits in enumerate(font[:95])}
glyphs.update({bits: chr(128+i) for i, bits in enumerate(font[95:])})

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
deadline = time.monotonic() + 45
while True:
    try:
        sock.connect(str(monitor))
        break
    except OSError:
        if time.monotonic() > deadline:
            raise RuntimeError("QEMU monitor socket unavailable")
        time.sleep(0.2)


def reply():
    data = bytearray()
    until = time.monotonic() + 10
    while time.monotonic() < until:
        sock.settimeout(0.5)
        try:
            part = sock.recv(16384)
        except socket.timeout:
            continue
        if not part:
            raise RuntimeError("QEMU monitor disconnected")
        data.extend(part)
        if b"(qemu) " in data:
            return bytes(data)
    raise RuntimeError("QEMU monitor command timed out")


def command(line):
    sock.sendall((line + "\n").encode("ascii"))
    r = reply()
    if b"Error" in r or b"error:" in r:
        raise RuntimeError(f"QEMU monitor {line!r}: {r!r}")


def key(name):
    command("sendkey " + name)
    time.sleep(0.16)


def focus(number):
    key(f"ctrl-alt-f{number}")
    time.sleep(0.4)


def screen(label):
    file = out / f"{label}.ppm"
    command(f"screendump {file}")
    img = Image.open(file).convert("RGB")
    width, height = img.size
    pixels = img.load()
    lines = []
    # Current kernel VGA font is 12x24, text begins at y=24.
    for row in range(min((height-24)//24, 256)):
        chars = []
        for col in range(min(width//12, 512)):
            bits = []
            for y in range(24+row*24, 24+(row+1)*24):
                value = 0
                for x in range(12*col, 12*(col+1)):
                    r, g, b = pixels[x, y]
                    value = (value << 1) | (r+g+b >= 500)
                bits.append(value)
            chars.append(glyphs.get(tuple(bits), "?"))
        lines.append("".join(chars).rstrip())
    text = "\n".join(lines)
    (out / f"{label}.txt").write_text(text, encoding="utf-8")
    return text


def waitscreen(label, expected, timeout=100):
    end = time.monotonic() + timeout
    last = ""
    while time.monotonic() < end:
        last = screen(label)
        if expected in last:
            return last
        if "panic:" in serial.read_text(errors="replace"):
            raise AssertionError("kernel panic: " + serial.read_text(errors="replace")[-1800:])
        time.sleep(1)
    raise AssertionError(f"{label}: missing {expected!r}\n{last[-2200:]}")


try:
    reply()  # Initial monitor greeting.
    waitscreen("system-boot", "kernel>", 120)
    focus(5)
    ready = waitscreen("f5-ready", "PLAN2001-BUSYBOX-F5-READY")
    assert "busybox>" in ready
    for letter in "hello":
        key(letter)
    key("ret")
    result = waitscreen("f5-input-output", "PLAN2001-BUSYBOX-F5-READ:hello")
    assert "PLAN2001-BUSYBOX-F5-READY" in result
    focus(1)
    system = waitscreen("system-isolated", "kernel>")
    assert "PLAN2001-BUSYBOX-F5-READ:hello" not in system, "stdout leaked into SystemConsole"
    focus(5)
    restored = waitscreen("f5-restored", "PLAN2001-BUSYBOX-F5-READ:hello")
    assert "busybox>" in restored
    print("PASS: real BusyBox upstream echo output + F5 PS/2 stdin + isolated VGA history")
finally:
    sock.close()
