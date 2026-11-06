#!/bin/sh
# Buildroot post-image script for the WT41N0.
#   1. Complete the device tree: kernel command line for the chosen boot
#      verbosity, and where the loader puts the initramfs.
#   2. Wrap loader + kernel + device tree + initramfs into the flashable
#      wt41n0-linux.hex.
set -eu
BOARD=$(cd "$(dirname "$0")" && pwd)
EXT=$(cd "$BOARD/../../.." && pwd)
IMAGES=$BINARIES_DIR
DTB=$IMAGES/omap4-wt41n0.dtb
CPIO=$IMAGES/rootfs.cpio.gz
FDTGET=$HOST_DIR/bin/fdtget
FDTPUT=$HOST_DIR/bin/fdtput

cfg_bool() { grep -q "^$1=y" "$BR2_CONFIG"; }
cfg_str() {
	sed -n "s/^$1=\"\(.*\)\"$/\1/p" "$BR2_CONFIG" | sed "s|\$(BR2_EXTERNAL_WT41N0_PATH)|$EXT|g"
}

# The device tree carries the fixed part of the command line; add the verbosity.
if cfg_bool BR2_WT41N0_BOOT_DEBUG; then
	extra="earlyprintk keep_bootcon ignore_loglevel"
elif cfg_bool BR2_WT41N0_BOOT_VERBOSE; then
	extra="loglevel=7"
else
	extra="quiet loglevel=3 vt.global_cursor_default=0"
fi
base=$("$FDTGET" -t s "$DTB" /chosen bootargs | sed -e 's/ quiet.*$//' -e 's/ loglevel=.*$//' -e 's/ earlyprintk.*$//')
"$FDTPUT" -t s "$DTB" /chosen bootargs "$base $extra"

# Initial entropy for the kernel (BR2_WT41N0_RNG_SEED, see its help text).
if cfg_bool BR2_WT41N0_RNG_SEED; then
	"$FDTPUT" -t bx "$DTB" /chosen rng-seed $(head -c 64 /dev/urandom | od -An -v -tx1)
fi

# Bluetooth needs the WL1283 init script, which this tree cannot ship (see
# local/README.md). Without it the driver retries by resetting the Bluetooth
# core, which also crashes Wi-Fi on this shared chip: switch Bluetooth off.
BT_NODE=/ocp/interconnect@48000000/segment@0/target-module@6a000/serial@0/bluetooth
if [ ! -s "$TARGET_DIR/lib/firmware/ti-connectivity/TIInit_10.6.15.bts" ]; then
	"$FDTPUT" -t s "$DTB" "$BT_NODE" status disabled
	echo "WT41N0: no Bluetooth init script in local/firmware/: Bluetooth disabled in the device tree"
fi

# The loader copies the initramfs to 0x89000000.
size=$(stat -c %s "$CPIO")
"$FDTPUT" -t x "$DTB" /chosen linux,initrd-start 89000000
"$FDTPUT" -t x "$DTB" /chosen linux,initrd-end "$(printf '%x' $((0x89000000 + size)))"

python3 "$EXT/support/mkhex.py" \
	--loader "$IMAGES/wt41n0-loader.bin" \
	--zimage "$IMAGES/zImage" \
	--dtb "$DTB" \
	--initramfs "$CPIO" \
	--entry-out "$IMAGES/wt41n0-linux.entry.bin" \
	--blob-out "$IMAGES/wt41n0-linux.blob.bin" \
	-o "$IMAGES/wt41n0-linux.hex"

# The eMMC system's boot image: the same kernel and the whole RAM system as its
# initramfs, with /init replaced by board/zebra/wt41n0/emmc-init. That script
# switches to the eMMC root file system, or stays in the RAM system as the recovery
# system (ESC held at power-on, /boot/recovery-once, or no usable eMMC root). The
# command line only defines the root partition (blkdevparts); the eMMC has no
# partition table Linux could read.
EMMC_DTB=$IMAGES/omap4-wt41n0-emmc.dtb
EMMC_CPIO=$IMAGES/rootfs-emmc-boot.cpio.gz
cp "$DTB" "$EMMC_DTB"
"$FDTPUT" -t s "$EMMC_DTB" /chosen bootargs \
	"$("$FDTGET" -t s "$DTB" /chosen bootargs) blkdevparts=mmcblk1:1687M@201M(rootfs)"
python3 - "$IMAGES/rootfs.cpio" "$BOARD/emmc-init" "$EMMC_CPIO" <<'EOF'
import gzip, sys
base, init, out = sys.argv[1:]
def entry(name, mode, data=b''):
    name = name.encode() + b'\0'
    fields = (0, mode, 0, 0, 1, 0, len(data), 0, 0, 0, 0, len(name), 0)
    hdr = ('070701' + ''.join(f'{v:08X}' for v in fields)).encode() + name
    hdr += b'\0' * (-len(hdr) % 4)
    return hdr + data + b'\0' * (-len(data) % 4)
# A second archive after the first: the kernel unpacks them in order, so this /init
# replaces the RAM system's.
extra = entry('init', 0o100755, open(init, 'rb').read()) + entry('boot', 0o40755) + entry('TRAILER!!!', 0)
open(out, 'wb').write(gzip.compress(open(base, 'rb').read() + extra, compresslevel=9, mtime=0))
EOF
size=$(stat -c %s "$EMMC_CPIO")
"$FDTPUT" -t x "$EMMC_DTB" /chosen linux,initrd-end "$(printf '%x' $((0x89000000 + size)))"
python3 "$EXT/support/mkhex.py" \
	--loader "$IMAGES/wt41n0-loader.bin" \
	--zimage "$IMAGES/zImage" \
	--dtb "$EMMC_DTB" \
	--initramfs "$EMMC_CPIO" \
	--entry-out "$IMAGES/wt41n0-emmc.entry.bin" \
	--blob-out "$IMAGES/wt41n0-emmc.blob.bin" \
	-o "$IMAGES/wt41n0-emmc.hex"

# Optional: run the loader in an emulator when the "unicorn" module is around.
if python3 -c 'import unicorn' 2>/dev/null; then
	for img in wt41n0-linux wt41n0-emmc; do
		python3 "$EXT/support/emulate_loader.py" "$IMAGES/$img.entry.bin" "$IMAGES/$img.blob.bin" | tail -1
	done
fi
echo "WT41N0: flash with  support/flash.py $IMAGES/wt41n0-linux.hex"
echo "WT41N0: install on the eMMC with  support/install.py $IMAGES --host root@<address>"
