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
import shlex
import sys
import time
import tty

MARK = b'__WT41N0_'


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
    try:
        # Turn off the shell's echo, so the reply holds only the command's output (the
        # echoed command line could be wrapped by the line editor), and drop what is
        # buffered. The markers are split in the command text, so only the output of
        # echo contains them.
        os.write(fd, b'stty -echo\n')
        time.sleep(0.3)
        drain(fd)
        os.write(fd, b'echo "%s""BEGIN__"; sh -c %s; echo "%s""END__"\n'
                 % (MARK, shlex.quote(args.command).encode(), MARK))
        out, deadline = b'', time.time() + args.wait
        while time.time() < deadline and MARK + b'END__' not in out:
            if select.select([fd], [], [], 0.2)[0]:
                out += drain(fd)
    finally:
        os.write(fd, b'stty echo\n')
        os.close(fd)

    text = out.replace(b'\r', b'').decode(errors='replace')
    text = text.split(MARK.decode() + 'BEGIN__\n', 1)[-1]
    done = MARK.decode() + 'END__' in text
    print(text.split(MARK.decode() + 'END__', 1)[0].rstrip())
    if not done:
        sys.exit(f'(no end marker after {args.wait:g} s: output may be incomplete, try --wait)')


def drain(fd):
    data = b''
    try:
        while True:
            chunk = os.read(fd, 65536)
            if not chunk:
                break
            data += chunk
    except BlockingIOError:
        pass
    return data

if __name__ == '__main__':
    main()
