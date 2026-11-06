#!/usr/bin/env python3
"""Install a WT41N0 image on a running unit over ssh, without download mode.

    support/update.py output/images/wt41n0-linux.hex [--host root@<address>] [--no-reboot]

Decodes the HEX file to the image, streams it to wt41n0-update on the unit, which
checks it, writes it to the eMMC OS area (where download mode puts it), verifies it,
and reboots. If the new image does not boot, use download mode and flash.py as usual.
"""
import argparse
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mkhex import decode_hex  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('hex_file', type=Path)
    parser.add_argument('--host', default='root@wt41n0', help='ssh destination (default root@wt41n0)')
    parser.add_argument('--no-reboot', action='store_true', help='write and verify only')
    parser.add_argument('--ssh', default='ssh -o ServerAliveInterval=5 -o ServerAliveCountMax=3',
                        help='ssh command (default: ssh with keepalives)')
    args = parser.parse_args()

    image = decode_hex(args.hex_file.read_bytes())
    print(f'update: {args.hex_file} -> {len(image)} byte image')
    cmd = 'wt41n0-update' + ('' if args.no_reboot else ' --reboot')
    result = subprocess.run(args.ssh.split() + [args.host, cmd], input=image)
    sys.exit(result.returncode)


if __name__ == '__main__':
    main()
