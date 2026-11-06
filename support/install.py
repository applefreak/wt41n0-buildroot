#!/usr/bin/env python3
"""Install the eMMC system on a WT41N0 over ssh.

    support/install.py output/images --host root@<address> [--yes]

Takes rootfs.ext4.gz (or rootfs.ext2.gz) and wt41n0-emmc.hex from the images
directory and:
  1. if the unit runs from its eMMC root, installs the eMMC boot image and reboots
     into its recovery system (/boot/recovery-once), so the root partition is free;
  2. writes the root filesystem to the eMMC from 201 MiB on (wt41n0-install-root);
  3. installs the eMMC boot image (wt41n0-emmc.hex) and reboots into it.
The unit can start from the RAM system (wt41n0-linux.hex) or the eMMC system.

Step 2 erases the Windows CE Application and Platform partitions; it asks first
unless --yes is given. Download mode and flash.py remain the way back.
"""
import argparse
import gzip
import hashlib
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mkhex import decode_hex  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('images', type=Path, help='Buildroot images directory')
    parser.add_argument('--host', default='root@wt41n0', help='ssh destination')
    parser.add_argument('--ssh', default='ssh -o ServerAliveInterval=5 -o ServerAliveCountMax=3',
                        help='ssh command (default: ssh with keepalives)')
    parser.add_argument('--yes', action='store_true', help='do not ask before erasing')
    args = parser.parse_args()

    ssh = args.ssh.split() + [args.host]
    rootfs = next((p for p in (args.images / 'rootfs.ext4.gz', args.images / 'rootfs.ext2.gz') if p.exists()), None)
    emmc_hex = args.images / 'wt41n0-emmc.hex'
    for p in (rootfs, emmc_hex):
        if p is None or not p.exists():
            sys.exit(f'install: missing {p or "rootfs.ext4.gz"} in {args.images}')

    def run(cmd, data=None, check=True):
        r = subprocess.run(ssh + [cmd], input=data, capture_output=data is None)
        if check and r.returncode:
            sys.exit(f'install: "{cmd}" failed ({r.returncode})')
        return r

    def wait_for_unit():
        time.sleep(15)
        for _ in range(60):
            if subprocess.run(ssh + ['true'], capture_output=True).returncode == 0:
                return
            time.sleep(5)
        sys.exit('install: the unit did not come back')

    if not args.yes:
        answer = input('This erases the Windows CE Application and Platform partitions. Continue? [y/N] ')
        if answer.strip().lower() != 'y':
            sys.exit('install: cancelled')

    def on_emmc():
        return run("grep -c '^/dev/mmcblk1p1 / ' /proc/mounts", check=False).stdout.strip() == b'1'

    emmc_image = decode_hex(emmc_hex.read_bytes())
    if on_emmc():
        print('install: the unit runs from its eMMC root; rebooting into the recovery system')
        run('wt41n0-update', emmc_image)
        run('mkdir -p /boot && touch /boot/recovery-once && sync')
        run('(sleep 2; reboot) </dev/null >/dev/null 2>&1 &', check=False)
        wait_for_unit()
        if on_emmc():
            sys.exit('install: the unit did not start the recovery system')

    print(f'install: checking {rootfs.name}')
    h, size = hashlib.sha256(), 0
    with gzip.open(rootfs, 'rb') as f:
        while chunk := f.read(1 << 20):
            h.update(chunk)
            size += len(chunk)
    header = f'WT41N0ROOT {size} {h.hexdigest()}\n'.encode()
    print(f'install: sending the root filesystem ({size >> 20} MiB, {rootfs.stat().st_size >> 20} MiB compressed)')
    run('wt41n0-install-root --yes', header + rootfs.read_bytes())

    print('install: installing the eMMC boot image')
    run('wt41n0-update --reboot', emmc_image, check=False)
    wait_for_unit()
    if not on_emmc():
        sys.exit('install: the unit did not come back up on the eMMC system')
    print('install: done; the unit runs from its eMMC root '
          '(the file system grows to fill the partition on this first boot)')


if __name__ == '__main__':
    main()
