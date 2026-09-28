"""Pretend to be the Hyperion Android grabber, to test the receiver without a TV.

Speaks the same protocol as Hyperion Android Reborn: a FlatBuffers Register,
then a stream of RawImage frames on TCP 19400.

    pip install flatbuffers requests
    python fake_grabber.py verify            # checks LED colours per side, exits non-zero on failure
    python fake_grabber.py demo --seconds 30 # moving rainbow, to watch the strip or the web preview
"""

import argparse
import colorsys
import socket
import struct
import sys
import time

import flatbuffers
import flatbuffers.number_types as nt
import flatbuffers.table
import requests

# Union ids from hyperion_request.fbs
CMD_IMAGE, CMD_REGISTER = 2, 4
IMG_RAW = 1


def _finish_request(b, cmd_type, cmd):
    b.StartObject(2)  # Request { command_type, command }
    b.PrependUint8Slot(0, cmd_type, 0)
    b.PrependUOffsetTRelativeSlot(1, cmd, 0)
    b.Finish(b.EndObject())
    return bytes(b.Output())


def register_msg(origin, priority):
    b = flatbuffers.Builder(64)
    o = b.CreateString(origin)
    b.StartObject(2)  # Register { origin, priority }
    b.PrependUOffsetTRelativeSlot(0, o, 0)
    b.PrependInt32Slot(1, priority, 0)
    return _finish_request(b, CMD_REGISTER, b.EndObject())


def image_msg(rgb, w, h):
    b = flatbuffers.Builder(len(rgb) + 128)
    data = b.CreateByteVector(rgb)
    b.StartObject(3)  # RawImage { data, width, height }
    b.PrependUOffsetTRelativeSlot(0, data, 0)
    b.PrependInt32Slot(1, w, -1)
    b.PrependInt32Slot(2, h, -1)
    raw = b.EndObject()
    b.StartObject(3)  # Image { data_type, data, duration }
    b.PrependUint8Slot(0, IMG_RAW, 0)
    b.PrependUOffsetTRelativeSlot(1, raw, 0)
    b.PrependInt32Slot(2, -1, -1)
    return _finish_request(b, CMD_IMAGE, b.EndObject())


def send(sock, msg):
    sock.sendall(struct.pack(">I", len(msg)) + msg)


def read_reply(sock):
    size = struct.unpack(">I", _recv_exact(sock, 4))[0]
    buf = _recv_exact(sock, size)
    t = flatbuffers.table.Table(buf, flatbuffers.encode.Get(nt.UOffsetTFlags.packer_type, buf, 0))
    o = t.Offset(8)  # slot 2: registered
    return t.Get(nt.Int32Flags, o + t.Pos) if o else -1


def _recv_exact(sock, n):
    out = b""
    while len(out) < n:
        chunk = sock.recv(n - len(out))
        if not chunk:
            raise ConnectionError("receiver closed the connection")
        out += chunk
    return out


def connect(host, port, priority=100):
    ip = socket.gethostbyname(host)
    sock = socket.create_connection((ip, port), timeout=5)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    send(sock, register_msg("FakeGrabber", priority))
    registered = read_reply(sock)
    if registered != priority:
        raise RuntimeError(f"register reply said {registered}, expected {priority}")
    print(f"Connected to {host} ({ip}):{port}, registered at priority {registered}")
    return sock, ip


def sides_frame(w, h):
    """Left third red, right third blue, top band in the middle green, rest black."""
    px = bytearray(w * h * 3)
    for y in range(h):
        for x in range(w):
            if x < w // 4:
                c = (255, 0, 0)
            elif x >= w - w // 4:
                c = (0, 0, 255)
            elif y < h // 4:
                c = (0, 255, 0)
            else:
                continue
            px[(y * w + x) * 3:(y * w + x) * 3 + 3] = bytes(c)
    return bytes(px)


def rainbow_frame(w, h, t):
    px = bytearray(w * h * 3)
    for x in range(w):
        r, g, b = colorsys.hsv_to_rgb((x / w + t) % 1.0, 1.0, 1.0)
        col = bytes((int(r * 255), int(g * 255), int(b * 255)))
        for y in range(h):
            px[(y * w + x) * 3:(y * w + x) * 3 + 3] = col
    return bytes(px)


def dominant(r, g, b):
    return "RGB"[max(range(3), key=lambda i: (r, g, b)[i])] if max(r, g, b) > 40 else "-"


def verify(args):
    sock, ip = connect(args.host, args.port)
    base = f"http://{ip}"
    cfg = requests.get(f"{base}/api/config", timeout=5).json()
    w, h = args.width, args.height
    frame = image_msg(sides_frame(w, h), w, h)

    end = time.time() + 2.0  # long enough for smoothing to settle
    sent = 0
    while time.time() < end:
        send(sock, frame)
        sent += 1
        time.sleep(1 / 30)
    time.sleep(0.3)

    status = requests.get(f"{base}/api/status", timeout=5).json()
    px = requests.get(f"{base}/api/leds", timeout=5).content
    sock.close()

    nl, ntop, nr = cfg["n_left"], cfg["n_top"], cfg["n_right"]
    leds = [dominant(*px[i:i + 3]) for i in range(0, len(px), 3)]
    if cfg["start_corner"] == 0:
        left, top, right = leds[:nl], leds[nl:nl + ntop], leds[nl + ntop:]
    else:
        right, top, left = leds[:nr], leds[nr:nr + ntop], leds[nr + ntop:]
        top = top[::-1]

    print(f"Sent {sent} frames of {w}x{h}; receiver reports {status['frames']} frames, "
          f"{status['width']}x{status['height']}, signal={status['signal']}")
    print(f"left  {''.join(left)}")
    print(f"top   {''.join(top)}")
    print(f"right {''.join(right)}")

    quarter = ntop // 4
    checks = {
        "LED count matches config": len(leds) == nl + ntop + nr,
        "receiver saw the frame size": (status["width"], status["height"]) == (w, h),
        "left side is red": set(left) == {"R"},
        "right side is blue": set(right) == {"B"},
        "top middle is green": set(top[quarter + 1:ntop - quarter - 1]) == {"G"},
        "top ends match the sides": top[0] == "R" and top[-1] == "B",
    }
    for name, ok in checks.items():
        print(f"  [{'PASS' if ok else 'FAIL'}] {name}")
    return 0 if all(checks.values()) else 1


def demo(args):
    sock, _ = connect(args.host, args.port)
    w, h = args.width, args.height
    start = time.time()
    frames = 0
    while time.time() - start < args.seconds:
        send(sock, image_msg(rainbow_frame(w, h, (time.time() - start) / 5), w, h))
        frames += 1
        time.sleep(1 / args.fps)
    sock.close()
    print(f"Sent {frames} frames in {args.seconds}s")
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("mode", choices=["verify", "demo"])
    p.add_argument("--host", default="tv-ambilight.local")
    p.add_argument("--port", type=int, default=19400)
    p.add_argument("--width", type=int, default=64)
    p.add_argument("--height", type=int, default=36)
    p.add_argument("--seconds", type=float, default=20)
    p.add_argument("--fps", type=float, default=30)
    args = p.parse_args()
    sys.exit(verify(args) if args.mode == "verify" else demo(args))


if __name__ == "__main__":
    main()
