# How the WT41N0 boots Linux

The unit keeps its stock boot chain. Nothing in it is replaced, and there is no U-Boot.

```
OMAP4 ROM  ->  x-loader  ->  Symbol bootloader ("monitor")  ->  OS image
                                                                 = our loader -> Linux
```

## The OS image

The monitor loads an OS image from the unit's OS storage area and jumps to a fixed
entry offset inside it. It also offers **Download from USB**, which receives a new OS
image in Zebra's HEX format (USB device `05e0:200b`) and stores it. This BSP installs
Linux that way.

`support/mkhex.py` builds the image, with nothing taken from the stock OS:

| Offset | Content |
|---|---|
| `0x00000` | a branch to the entry point, the image signature `ECEC` at `0x40`, zeros |
| `0x11880` | OS entry point: the loader (`package/wt41n0-loader`), then zero padding |
| `0x20000` | "blob": three little-endian sizes, then `zImage`, the device tree, the initramfs |

The monitor checks for the `ECEC` signature (the marker of a Windows CE image) and
reports "error reading OS image, image not present" without it. It needs nothing else
from a CE image: the two pointers that normally follow the signature can be zero.

The HEX file itself starts with a size record, five fixed settings records and a
version record, followed by plain data records.

## The loader

At every boot the monitor reads a fixed `0x77F8000` bytes of the OS area (all of it but
the last 32 KiB) to `0x80268000`, whatever the image size, switches the MMU and caches
off and jumps to the image's first word, a branch to the entry point. (The stock
Windows CE image is linked to run at `0x81168000`; there is no second copy there.) So:

- the loader is linked for the staged copy (entry `0x80279880`); its first instruction
  is a pc-relative jump to its linked address,
- it checks a word-sum of itself before doing anything else.

It then:

1. shows its progress on the LCD: image sizes, then one line per step below (in the
   debug profile: a register dump and a colour per stage),
2. cleans and disables the PL310 L2 cache through ROM service `0x102` (the monitor
   leaves it on; the kernel's decompressor only handles L1, and a live L2 makes the
   kernel start randomly),
3. copies the initramfs to `0x89000000`, the device tree to `0x88000000` and the
   kernel to `0x82008000`, in that order (the kernel's destination lies inside a large
   staged image), checking magic values,
4. jumps to the kernel with `r2` = device tree address.

**Size limit.** The image can use what the monitor loads: `0x77F8000` bytes (125.8 MB),
ending at `0x87A60000`. The loader's stack (top `0x88000000`), the device tree and the
initramfs destinations lie above that; `mkhex.py` refuses a larger image, and the
loader accepts an initramfs of up to 64 MiB. A 57 MB image has been booted on hardware.

`post-image.sh` writes the matching `linux,initrd-start` / `linux,initrd-end` and the
kernel command line into the device tree, and `mkhex.py` refuses to build an image
whose device tree does not match.

## RAM system and eMMC system

Both boot the same way up to the kernel, with the same root file system in the
initramfs:

- **RAM system** (`wt41n0-linux.hex`): runs from the initramfs.
- **eMMC system** (`wt41n0-emmc.hex`): its initramfs `/init` (`emmc-init`) mounts the
  ext4 root at `/dev/mmcblk1p1` and switches to it; otherwise it stays in the RAM
  system as the recovery system. The eMMC has no partition table Linux could read;
  `blkdevparts=mmcblk1:1687M@201M(rootfs)` on the command line defines the partition.
- **Installer** (`wt41n0-install.hex`): the eMMC system's image with `wt41n0.install`
  on the command line. When the root partition holds no ext2/3/4 file system, `/init`
  first creates one and copies the initramfs into it (`wt41n0-install-root --self`),
  then boots it. With a root file system present it behaves like `wt41n0-emmc.hex`.

A running system can install either image into the OS area itself (`wt41n0-update`),
which is what download mode does too.

## What Linux inherits

The kernel relies on state the stock boot chain leaves behind, and the device tree is
written accordingly:

- **Display**: the panel is already running. Linux uses it as a `simple-framebuffer`
  (`0x9B000000`, 240x320 RGB565, rotated) and never probes the display controller;
  resetting it blanks the LCD.
- **Pin multiplexing and DDR**: untouched. The device tree has almost no pinmux entries.
- **GPIO outputs**: the kernel resets the GPIO blocks, so `gpio-hog` nodes restore the
  levels the bootloader drove. GPIO 41 enables the FPGA.
- **Power rails**: all PMIC rails are kept on as found.

## Emulation

`support/emulate_loader.py` runs the loader in a CPU emulator with the monitor's memory
layout (the whole image staged at `0x80268000` and entered there) and checks the
hand-off to the kernel. A broken
loader leaves the unit with a blank or frozen screen, so run it after any loader change
(`pip install unicorn`). `post-image.sh` runs it automatically when the module is installed.
