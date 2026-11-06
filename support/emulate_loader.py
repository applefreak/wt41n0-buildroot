#!/usr/bin/env python3
"""Run the Linux loader in a unicorn ARM emulator and check the kernel handoff.

The Symbol monitor stages the OS image at 0x80268000 and makes a partial run
copy whose entry point is 0x81179880 (see docs/boot-flow.md). This script
reproduces that layout from the entry file (loader + padding + blob), runs the
loader until it jumps to the kernel, and checks that the zImage, DTB and
initramfs landed at their destinations unchanged and that r0/r1/r2 are correct.

Optional check for people who change the loader. Needs the "unicorn" Python
module. The two input files come from mkhex.py --entry-out / --blob-out.

Usage:
    support/emulate_loader.py wt41n0-linux.entry.bin wt41n0-linux.blob.bin
"""
import argparse
import struct
import sys
from pathlib import Path

from unicorn import (
    Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_UNMAPPED, UC_HOOK_INTR,
    UC_HOOK_MEM_WRITE, UC_HOOK_MEM_READ,
)
from unicorn.arm_const import (
    UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_SP,
    UC_ARM_REG_R12,
)

RAM_BASE = 0x80000000
RAM_SIZE = 0x20000000
STAGING_BASE = 0x80268000
ENTRY_OFFSET = 0x11880            # OS entry point inside the image
LOADER_ADDR = 0x81179880          # monitor jumps here (run copy)
RUN_COPY_INTACT = 0x780           # only this much of the run copy is trusted
STAGING_LOADER = STAGING_BASE + ENTRY_OFFSET
PL310_BASE = 0x48242000
BLOB_FILE_OFFSET = 0x20000
BLOB_ADDR = STAGING_BASE + BLOB_FILE_OFFSET
KERNEL_ADDR = 0x82008000
DTB_ADDR = 0x88000000
INITRD_ADDR = 0x89000000
FB_ADDR = 0x9B000000
FB_SIZE = 0x25800


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('entry', type=Path, help='entry file from mkhex.py --entry-out')
    parser.add_argument('blob', type=Path, help='blob file from mkhex.py --blob-out')
    parser.add_argument('--max-instructions', type=int, default=400_000_000)
    args = parser.parse_args()

    entry = args.entry.read_bytes()
    blob = args.blob.read_bytes()
    blob_offset = BLOB_FILE_OFFSET - ENTRY_OFFSET
    if entry[blob_offset:] != blob:
        sys.exit('entry file does not contain the blob at the expected offset')
    zlen, dlen, ilen = struct.unpack_from('<III', blob, 0)
    zimage = blob[12:12 + zlen]
    dtb = blob[12 + zlen:12 + zlen + dlen]
    initrd = blob[12 + zlen + dlen:12 + zlen + dlen + ilen]
    if len(initrd) != ilen:
        sys.exit('blob shorter than header lengths')
    loader = entry[:blob_offset].rstrip(b'\0')

    mu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    mu.mem_map(RAM_BASE, RAM_SIZE)
    # Staged image (the part in front of the entry point does not matter here).
    mu.mem_write(STAGING_BASE + ENTRY_OFFSET, entry)
    # Run copy made by the monitor: only the first 0x780 bytes are reliable,
    # so model exactly that and fill the rest with junk.
    mu.mem_write(LOADER_ADDR, loader[:RUN_COPY_INTACT])
    mu.mem_write(LOADER_ADDR + RUN_COPY_INTACT, b'\xA5' * (len(loader) - RUN_COPY_INTACT))
    # PL310 L2 controller, modelled as enabled so the disable path runs.
    mu.mem_map(PL310_BASE, 0x1000)
    mu.mem_write(PL310_BASE + 0x100, struct.pack('<I', 1))
    mu.mem_write(PL310_BASE + 0x104, struct.pack('<I', 0x7E470001))
    # DMM LISA map and EMIF1/EMIF2 SDRAM_CONFIG, read by the diagnostic screen.
    mu.mem_map(0x4E000000, 0x1000)
    mu.mem_write(0x4E000040, struct.pack('<IIII', 0x80500300, 0, 0, 0xFF020100))
    mu.mem_map(0x4C000000, 0x1000)
    mu.mem_map(0x4D000000, 0x1000)
    for b in (0x4A310000, 0x48055000, 0x48057000, 0x48059000, 0x4805B000, 0x4805D000):
        mu.mem_map(b, 0x1000)                      # GPIO banks
    mu.mem_map(0x50000000, 0x1000)                 # GPMC
    mu.mem_map(0x02000000, 0x01000000)             # FPGA window (reads as zero)
    # USB OTG (MUSB), control module, CM2 L3INIT/DPLL registers (read as zero)
    mu.mem_map(0x4A0AB000, 0x1000)
    mu.mem_map(0x4A002000, 0x1000)
    mu.mem_map(0x4A009000, 0x1000)
    mu.mem_map(0x4A008000, 0x1000)
    # Fill the framebuffer with a pattern, to notice if nothing is drawn.
    mu.mem_write(FB_ADDR, b'\x55' * FB_SIZE)

    state = {'count': 0, 'reached': False, 'fault': None, 'smc': [], 'l2_cleaned': False}

    def on_pl310_write(uc, access, address, size, value, user_data):
        if address == PL310_BASE + 0x7FC and value == 0xFFFF:
            state['l2_cleaned'] = True

    def on_pl310_read(uc, access, address, size, value, user_data):
        # Maintenance registers complete immediately: polls read back 0.
        if address in (PL310_BASE + 0x7FC, PL310_BASE + 0x730):
            uc.mem_write(address, b'\0\0\0\0')

    def on_intr(uc, intno, user_data):
        pc = uc.reg_read(UC_ARM_REG_PC)
        service = uc.reg_read(UC_ARM_REG_R12)
        arg = uc.reg_read(UC_ARM_REG_R0)
        state['smc'].append((service, arg))
        if service == 0x102:
            uc.mem_write(PL310_BASE + 0x100, struct.pack('<I', arg & 1))
        # unicorn leaves pc at the smc; step over it
        uc.reg_write(UC_ARM_REG_PC, pc + 4 if bytes(uc.mem_read(pc, 4)) == b'\x70\x00\x60\xe1' else pc)

    def on_code(uc, address, size, user_data):
        state['count'] += 1
        if address == KERNEL_ADDR:
            state['reached'] = True
            uc.emu_stop()
        elif state['count'] > args.max_instructions:
            state['fault'] = f'instruction budget exhausted at pc=0x{address:08X}'
            uc.emu_stop()

    def on_unmapped(uc, access, address, size, value, user_data):
        state['fault'] = f'unmapped access at 0x{address:08X} pc=0x{uc.reg_read(UC_ARM_REG_PC):08X}'
        return False

    mu.hook_add(UC_HOOK_CODE, on_code)
    mu.hook_add(UC_HOOK_MEM_UNMAPPED, on_unmapped)
    mu.hook_add(UC_HOOK_MEM_WRITE, on_pl310_write, begin=PL310_BASE, end=PL310_BASE + 0xFFF)
    mu.hook_add(UC_HOOK_MEM_READ, on_pl310_read, begin=PL310_BASE, end=PL310_BASE + 0xFFF)
    mu.hook_add(UC_HOOK_INTR, on_intr)

    try:
        mu.emu_start(LOADER_ADDR, 0, count=0)
    except Exception as exc:  # unicorn raises UcError on faults
        if not state['fault']:
            state['fault'] = f'{exc} at pc=0x{mu.reg_read(UC_ARM_REG_PC):08X}'

    pc = mu.reg_read(UC_ARM_REG_PC)
    print(f'instructions executed: {state["count"]}')
    if state['fault']:
        sys.exit(f'FAIL: {state["fault"]}')
    if not state['reached'] or pc != KERNEL_ADDR:
        sys.exit(f'FAIL: loader stopped at pc=0x{pc:08X} without reaching the kernel')

    problems = []
    r0, r1, r2 = (mu.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2))
    if (r0, r1, r2) != (0, 0xFFFFFFFF, DTB_ADDR):
        problems.append(f'registers r0=0x{r0:X} r1=0x{r1:X} r2=0x{r2:X}')
    for name, addr, data in (('zImage', KERNEL_ADDR, zimage), ('DTB', DTB_ADDR, dtb), ('initramfs', INITRD_ADDR, initrd)):
        if bytes(mu.mem_read(addr, len(data))) != data:
            problems.append(f'{name} at 0x{addr:08X} does not match the blob')
        else:
            print(f'{name}: {len(data)} bytes verified at 0x{addr:08X}')
    fb = bytes(mu.mem_read(FB_ADDR, FB_SIZE))
    if fb == b'\x55' * FB_SIZE:
        problems.append('loader never wrote the framebuffer')
    else:
        colors = {}
        for i in range(0, FB_SIZE, 2):
            v = fb[i] | fb[i + 1] << 8
            colors[v] = colors.get(v, 0) + 1
        dominant = max(colors, key=colors.get)
        print(f'framebuffer dominant color 0x{dominant:04X} ({colors[dominant]} of {FB_SIZE // 2} pixels)')
        # Diagnostic loader: all-white handoff screen. Normal loader: black,
        # with a few lines of white progress text.
        text_pixels = sum(n for c, n in colors.items() if c != dominant)
        if dominant == 0xFFFF:
            print('screen: diagnostic loader, white handoff checkpoint')
        elif dominant == 0x0000 and 0 < text_pixels < FB_SIZE // 8:
            print(f'screen: progress text ({text_pixels} text pixels on black)')
        else:
            problems.append('final screen is neither the white handoff checkpoint nor the progress screen')
    sp = mu.reg_read(UC_ARM_REG_SP)
    print(f'final sp=0x{sp:08X}')
    print(f'secure monitor calls: {[(hex(s), a) for s, a in state["smc"]]}')
    l2ctrl = struct.unpack('<I', bytes(mu.mem_read(PL310_BASE + 0x100, 4)))[0]
    if not state['l2_cleaned'] or (0x102, 0) not in state['smc'] or l2ctrl != 0:
        problems.append('L2 cache was not cleaned and disabled')
    else:
        print('L2 cache: cleaned, invalidated and disabled via SMC 0x102')
    if problems:
        sys.exit('FAIL: ' + '; '.join(problems))
    print('PASS: loader reached the kernel entry with all images and registers correct')


if __name__ == '__main__':
    main()
