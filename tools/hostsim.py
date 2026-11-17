#!/usr/bin/env python3
"""Stream a JPEG to a udisp device over raw USB (no kernel driver needed).

Useful for bring-up: verifies the device's gadget, frame parser, decoder and
DRM output without installing Windows IDD / Linux udisp_drm.

    pip install pyusb
    sudo ./tools/hostsim.py test.jpg --fps 10 --seconds 15

The device must already be in udisp mode (VID:PID 303a:2986).
SPDX-License-Identifier: MIT
"""
import argparse
import glob
import os
import socket
import struct
import sys
import time

import usb.core
import usb.util

VID, PID = 0x303A, 0x2986
TYPE_JPEG = 3


def jpeg_size(data: bytes):
    i = 2
    n = len(data)
    while i + 1 < n:
        if data[i] != 0xFF:
            i += 1
            continue
        marker = data[i + 1]
        i += 2
        if marker in (0xD8, 0xD9) or 0xD0 <= marker <= 0xD7:
            continue
        if i + 2 > n:
            break
        seg = (data[i] << 8) | data[i + 1]
        if marker in (0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7,
                      0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF):
            h = (data[i + 3] << 8) | data[i + 4]
            w = (data[i + 5] << 8) | data[i + 6]
            return w, h
        i += seg
    return None


def find_device(timeout: float, vid=VID, pid=PID):
    deadline = time.time() + timeout
    while time.time() < deadline:
        dev = usb.core.find(idVendor=vid, idProduct=pid)
        if dev is not None:
            return dev
        time.sleep(0.3)
    return None


def open_bulk_out(dev):
    """Find the bulk OUT endpoint, preferring the vendor-specific interface."""
    try:
        dev.set_configuration()
    except usb.core.USBError:
        pass
    cfg = dev.get_active_configuration()
    fallback = None
    for intf in cfg:
        ep = usb.util.find_descriptor(
            intf,
            custom_match=lambda e: usb.util.endpoint_type(e.bmAttributes)
            == usb.util.ENDPOINT_TYPE_BULK
            and usb.util.endpoint_direction(e.bEndpointAddress)
            == usb.util.ENDPOINT_OUT,
        )
        if ep is None:
            continue
        # Our vendor interface: FF/00/00.  adb is FF/42/01 - never write there.
        if (intf.bInterfaceClass == 0xFF and intf.bInterfaceSubClass == 0
                and intf.bInterfaceProtocol == 0):
            return ep
        if intf.bInterfaceSubClass != 0x42:
            fallback = fallback or ep
    if fallback is None:
        raise SystemExit("no bulk OUT endpoint found")
    return fallback


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", help="JPEG file")
    ap.add_argument("--fps", type=float, default=10.0)
    ap.add_argument("--seconds", type=float, default=10.0)
    ap.add_argument("--frames", type=int, default=0, help="stop after N frames")
    ap.add_argument("--timeout", type=float, default=20.0, help="wait for device")
    ap.add_argument("--vid", default="303a", help="USB vendor id (hex)")
    ap.add_argument("--pid", default="2986", help="USB product id (hex)")
    ap.add_argument("--tcp", metavar="HOST:PORT",
                    help="send over TCP (e.g. via adb forward) instead of USB")
    args = ap.parse_args()

    if os.path.isdir(args.image):
        paths = sorted(glob.glob(os.path.join(args.image, "*.jpg")))
        if not paths:
            raise SystemExit("no *.jpg in directory")
    else:
        paths = [args.image]
    frames = [open(p, "rb").read() for p in paths]
    size = jpeg_size(frames[0])
    if not size:
        raise SystemExit("cannot parse JPEG dimensions")
    w, h = size
    print(f"{len(frames)} frame(s) {w}x{h}, {len(frames[0])} bytes each")

    if args.tcp:
        host, _, port = args.tcp.rpartition(":")
        deadline = time.time() + args.timeout
        sock = None
        while time.time() < deadline and sock is None:
            try:
                sock = socket.create_connection((host or "127.0.0.1", int(port)), timeout=3)
            except OSError:
                time.sleep(0.3)
        if sock is None:
            raise SystemExit(f"cannot connect to {args.tcp}")
        print(f"TCP {args.tcp}")

        def send(data):
            sock.sendall(data)
    else:
        dev = find_device(args.timeout, int(args.vid, 16), int(args.pid, 16))
        if dev is None:
            raise SystemExit(
                f"{args.vid}:{args.pid} not found (is the board in udisp mode?)")
        print(f"device on bus {dev.bus} addr {dev.address}")
        ep = open_bulk_out(dev)

        def send(data):
            dev.write(ep.bEndpointAddress, data, timeout=3000)

    period = 1.0 / args.fps if args.fps > 0 else 0.0
    deadline = time.time() + args.seconds
    sent = 0
    t0 = time.time()
    while time.time() < deadline:
        if args.frames and sent >= args.frames:
            break
        jpeg = frames[sent % len(frames)]
        packed = (sent & 0x3FF) | (len(jpeg) << 10)
        header = struct.pack("<HBBHHHHI", 0, TYPE_JPEG, 0, 0, 0, w, h, packed)
        try:
            send(header + jpeg)
        except (usb.core.USBError, OSError) as e:
            print(f"write failed at frame {sent}: {e}")
            time.sleep(0.5)
            continue
        sent += 1
        if sent % 10 == 0:
            dt = time.time() - t0
            print(f"sent {sent} frames ({sent/dt:.1f} fps)")
        if period:
            time.sleep(period)

    dt = time.time() - t0
    print(f"done: {sent} frames in {dt:.1f}s ({sent/dt:.1f} fps)")
    if not args.tcp:
        usb.util.dispose_resources(dev)


if __name__ == "__main__":
    main()
