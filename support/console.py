#!/usr/bin/env python3
"""Run a shell command on the WT41N0 over its USB serial console.

The unit runs a root shell on its USB serial gadget, so this works without any
network. The port is found automatically (/dev/ttyACM* on Linux,
/dev/cu.usbmodem* on macOS) unless --port is given.

    support/console.py 'dmesg | tail'
    support/console.py --wait 10 'wpa_cli -i wlan0 status'

For an interactive session use any terminal program, for example
"screen /dev/ttyACM0 115200".
"""
import argparse
import glob
import os
import select
import sys
import time
import tty

END = b'__WT41N0_END__'


def find_port():
    ports = sorted(glob.glob('/dev/ttyACM*') + glob.glob('/dev/cu.usbmodem*'))
    if not ports:
        sys.exit('no USB serial port found: is the cable connected and the unit booted?')
    return ports[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('command', help='shell command to run on the unit')
    parser.add_argument('--port', help='serial device (default: autodetect)')
    parser.add_argument('--wait', type=float, default=3, help='seconds to wait for the output')
    args = parser.parse_args()

    fd = os.open(args.port or find_port(), os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    tty.setraw(fd)
    # Start from an empty prompt and drop whatever is buffered.
    os.write(fd, b'\n')
    time.sleep(0.3)
    try:
        os.read(fd, 65536)
    except BlockingIOError:
        pass

    marker = b'echo ' + END
    os.write(fd, args.command.encode() + b'; ' + marker + b'\n')
    out, deadline = b'', time.time() + args.wait
    while time.time() < deadline and END + b'\r' not in out.split(marker, 1)[-1]:
        if select.select([fd], [], [], 0.2)[0]:
            try:
                out += os.read(fd, 65536)
            except BlockingIOError:
                pass
    os.close(fd)

    text = out.replace(b'\r', b'').decode(errors='replace')
    text = text.split(marker.decode() + '\n', 1)[-1]
    print(text.split(END.decode(), 1)[0].rstrip())


if __name__ == '__main__':
    main()
