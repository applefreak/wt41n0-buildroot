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

The monitor stages the image at `0x80268000` and makes a run copy at `0x81168000`,
then jumps to the entry in the run copy (`0x81179880`). Only the first `0x780` bytes of
that run copy are reliable, so:

- the loader is linked for the **staging** copy (`0x80279880`),
- its first instruction is a pc-relative jump there, and the literal it uses sits right
  after the vector table, inside the reliable part,
- it checks a word-sum of itself before doing anything else.

It then:

1. shows its progress on the LCD: image sizes, then one line per step below (in the
   debug profile: a register dump and a colour per stage),
2. cleans and disables the PL310 L2 cache through ROM service `0x102` (the monitor
   leaves it on; the kernel's decompressor only handles L1, and a live L2 makes the
   kernel start randomly),
3. copies the kernel to `0x82008000`, the device tree to `0x88000000` and the
   initramfs to `0x89000000`, checking magic values,
4. jumps to the kernel with `r2` = device tree address.

**Size limit.** The staged image starts at `0x80268000`; the loader's stack and the
monitor's run copy sit about 15 MB above it. The whole image (kernel + device tree +
initramfs) therefore has to stay below 15.2 MB, and `mkhex.py` refuses anything larger.
The default image is about 11 MB. A root filesystem on eMMC is the way past this limit.

`post-image.sh` writes the matching `linux,initrd-start` / `linux,initrd-end` and the
kernel command line into the device tree, and `mkhex.py` refuses to build an image
whose device tree does not match.

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
layout, including the truncated run copy, and checks the hand-off to the kernel. A broken
loader leaves the unit with a blank or frozen screen, so run it after any loader change
(`pip install unicorn`). `post-image.sh` runs it automatically when the module is installed.
