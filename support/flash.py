#!/usr/bin/env python3
"""Send a Symbol HEX image to a WT41N0 in "Download from USB" mode.

Put the unit in download mode first: hold P1 or P2 while switching it on, then
choose Download from USB. The unit shows up as USB device 05e0:200b. Needs the
"pyusb" Python module (pip install pyusb) and libusb.

    support/flash.py wt41n0-linux.hex

The image is stored in the unit's OS area and survives reboots. The unit does
not restart by itself afterwards: power-cycle it. To go back to the stock OS,
flash the stock HEX with this same tool.
"""
import argparse
import time
from pathlib import Path

import usb.core
import usb.util

from mkhex import parse_record

SYMBOL_VID = 0x05E0
DOWNLOAD_PID = 0x200B
CHUNK_SIZE = 1024
XON = 0x11
XOFF = 0x13


def validate_hex(data):
    offset = 0
    records = 0
    while offset < len(data):
        *_, offset = parse_record(data, offset)
        records += 1
    return records


def find_download_endpoints(device):
    try:
        configuration = device.get_active_configuration()
    except usb.core.USBError:
        device.set_configuration()
        configuration = device.get_active_configuration()

    for interface in configuration:
        endpoints = list(interface)
        bulk_out = [ep for ep in endpoints
                    if usb.util.endpoint_direction(ep.bEndpointAddress) == usb.util.ENDPOINT_OUT
                    and usb.util.endpoint_type(ep.bmAttributes) == usb.util.ENDPOINT_TYPE_BULK]
        bulk_in = [ep for ep in endpoints
                   if usb.util.endpoint_direction(ep.bEndpointAddress) == usb.util.ENDPOINT_IN
                   and usb.util.endpoint_type(ep.bmAttributes) == usb.util.ENDPOINT_TYPE_BULK]
        if bulk_out and bulk_in:
            return interface, bulk_out[0].bEndpointAddress, bulk_in[0].bEndpointAddress
    raise RuntimeError('no interface with both bulk OUT and bulk IN endpoints')


def wait_for_xon(device, endpoint):
    paused = False
    started = time.monotonic()
    last_message = started
    while True:
        try:
            value = device.read(endpoint, 1, timeout=1000)[0]
        except usb.core.USBError as error:
            if error.errno is not None:
                raise
            now = time.monotonic()
            if now - last_message >= 30:
                print(f'waiting for device flow control ({int(now - started)}s)', flush=True)
                last_message = now
            continue
        if value == XON:
            return
        if value == XOFF and not paused:
            print('device paused transfer (XOFF)', flush=True)
            paused = True


def download(device, data):
    interface, endpoint_out, endpoint_in = find_download_endpoints(device)
    number = interface.bInterfaceNumber
    try:
        if device.is_kernel_driver_active(number):
            device.detach_kernel_driver(number)
    except (NotImplementedError, usb.core.USBError):
        pass
    usb.util.claim_interface(device, number)
    print(f'interface={number} bulk_out=0x{endpoint_out:02X} bulk_in=0x{endpoint_in:02X}', flush=True)

    sent = 0
    next_report = 10
    while sent < len(data):
        if sent:
            wait_for_xon(device, endpoint_in)
        chunk = data[sent:sent + CHUNK_SIZE]
        written = device.write(endpoint_out, chunk, timeout=30000)
        if written != len(chunk):
            raise RuntimeError(f'short USB write: {written}/{len(chunk)} bytes')
        sent += written
        percent = sent * 100 // len(data)
        if percent >= next_report or sent == len(data):
            print(f'sent {sent}/{len(data)} bytes ({percent}%)', flush=True)
            next_report = (percent // 10 + 1) * 10

    time.sleep(0.05)
    device.write(endpoint_out, b'', timeout=5000)
    print('EOF sent; device is processing the image', flush=True)


def main():
    parser = argparse.ArgumentParser(description='Download a Symbol HEX image over bootloader USB')
    parser.add_argument('hex_file', type=Path)
    parser.add_argument('--vid', type=lambda value: int(value, 0), default=SYMBOL_VID)
    parser.add_argument('--pid', type=lambda value: int(value, 0), default=DOWNLOAD_PID)
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()

    data = args.hex_file.read_bytes()
    records = validate_hex(data)
    print(f'validated {args.hex_file}: {len(data)} bytes, {records} records')
    if args.dry_run:
        chunks = (len(data) + CHUNK_SIZE - 1) // CHUNK_SIZE
        print(f'dry run: {chunks} x <=1024-byte writes, XON/XOFF flow control, 50ms delay, zero-length EOF')
        return

    device = usb.core.find(idVendor=args.vid, idProduct=args.pid)
    if device is None:
        raise SystemExit(f'USB device {args.vid:04x}:{args.pid:04x} not found; enter Download from USB first')
    try:
        download(device, data)
    finally:
        usb.util.dispose_resources(device)


if __name__ == '__main__':
    main()
