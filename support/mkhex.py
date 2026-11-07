#!/usr/bin/env python3
"""Build a flashable Symbol HEX image for the WT41N0 from Buildroot's outputs.

    mkhex.py --loader loader.bin --zimage zImage --dtb omap4-wt41n0.dtb \\
             --initramfs rootfs.cpio.gz -o wt41n0-linux.hex

The Symbol monitor only accepts an OS image in its own HEX format. The image
this tool builds needs nothing from the stock OS:

    0x00000   a branch to the entry point; the image signature "ECEC" at 0x40
              (the monitor reports "image not present" without it); zeros
    0x11880   loader (the monitor's entry point), zero padding
    0x20000   blob: three little-endian sizes, then zImage, DTB, initramfs

Everything is checked before the file is written: the kernel, DTB and initramfs
magic values, the initrd addresses in the DTB, the loader's self-checksum, and
that the finished HEX decodes back to exactly the intended image.
"""
import argparse
import hashlib
import struct
import sys
from pathlib import Path

NK_ENTRY = 0x11880          # offset of the OS entry point inside the image
BLOB_OFFSET = 0x20000       # where the loader expects the blob
INITRD_ADDR = 0x89000000    # where the loader copies the initramfs
# The monitor reads this much of the OS area to 0x80268000 at every boot,
# whatever the image size; the loader's stack and copy destinations lie above.
MAX_IMAGE_SIZE = 0x77F8000      # 125.8 MB

# ---- Symbol HEX records ------------------------------------------------------
# ":CCAAAATT<data>SS". Types below 0x80 carry hex text (Intel-HEX style), types
# 0x80 and above carry raw bytes. 0x02 = segment, 0xA3 = high address,
# 0x82/0x84 = data, 0xA0 = header with the image size, 0x01 = end of file.

def parse_record(data, offset):
    if data[offset:offset + 1] != b':':
        raise ValueError(f'expected a record at 0x{offset:X}')
    count = int(data[offset + 1:offset + 3], 16)
    address = int(data[offset + 3:offset + 7], 16)
    record_type = int(data[offset + 7:offset + 9], 16)
    size = count if record_type >= 0x80 else count * 2
    start = offset + 9
    checksum = start + size
    next_offset = checksum + 2
    if data[next_offset:next_offset + 2] == b'\r\n':
        next_offset += 2
    elif data[next_offset:next_offset + 1] == b'\n':
        next_offset += 1
    else:
        raise ValueError(f'invalid line ending at 0x{offset:X}')
    expected = int(data[checksum:checksum + 2], 16)
    if record_type >= 0x80:
        computed = (-sum(data[offset + 1:checksum])) & 0xFF
    else:
        raw = bytes((count,)) + address.to_bytes(2, 'big') + bytes((record_type,))
        raw += bytes.fromhex(data[start:checksum].decode())
        computed = (-sum(raw)) & 0xFF
    if computed != expected:
        raise ValueError(f'checksum mismatch at 0x{offset:X}')
    return count, address, record_type, start, checksum, next_offset


def data_record(record_type, address, data):
    body = f'{len(data):02X}{address:04X}{record_type:02X}'.encode() + data
    return b':' + body + f'{(-sum(body)) & 0xFF:02X}'.encode() + b'\r\n'


def address_record(record_type, value):
    text = f'{value:04X}'.encode()
    count = len(text) if record_type >= 0x80 else len(text) // 2
    body = f'{count:02X}0000{record_type:02X}'.encode() + text
    if record_type >= 0x80:
        checksum = (-sum(body)) & 0xFF
    else:
        checksum = (-sum(bytes((count, 0, 0, record_type)) + value.to_bytes(2, 'big'))) & 0xFF
    return b':' + body + f'{checksum:02X}'.encode() + b'\r\n'


def decode_hex(data):
    """Decode a Symbol HEX file to the binary image it carries."""
    decoded = bytearray()
    offset = high = segment = 0
    while offset < len(data):
        count, address, record_type, start, checksum, next_offset = parse_record(data, offset)
        if record_type == 0x02:
            segment = int(data[start:checksum], 16)
        elif record_type == 0xA3:
            high = int(data[start:checksum], 16)
        elif record_type in (0x82, 0x84):
            position = (high << 20) + (segment << 4) + address
            if position > len(decoded):
                decoded += bytes(position - len(decoded))
            decoded[position:position + count] = data[start:start + count]
        elif record_type == 0x01:
            break
        offset = next_offset
    return bytes(decoded)


# Framing records every WT41N0 OS HEX starts with: five settings records and a
# version record. They do not depend on the image contents.
HEX_FRAMING = (
    b':130000A500000003st0000a003#25\r\n'
    b':130000A500000005st00000002#55\r\n'
    b':130000A500000000st00000004#58\r\n'
    b':130000A500000001st00000002#59\r\n'
    b':130000A500000002st00000025xFE\r\n'
    b':020000A1010B\r\n'
)


def image_prefix():
    """The NK_ENTRY bytes in front of the loader.

    The monitor looks for the Windows CE image signature "ECEC" at offset 0x40
    and otherwise reports "image not present"; it then starts the image at its
    first word, so that word is an ARM branch to the entry offset. The two
    pointers that follow the signature in a real CE image are not needed
    (zeros work; an image without the signature is rejected).
    """
    prefix = bytearray(NK_ENTRY)
    struct.pack_into('<I', prefix, 0, 0xEA000000 | ((NK_ENTRY - 8) // 4))   # b NK_ENTRY
    prefix[0x40:0x44] = b'ECEC'
    return bytes(prefix)


def build_hex(image):
    """A complete HEX file for `image`: size header, framing, data records."""
    size_text = b'19' + f'{len(image):08X}'.encode()
    body = b'0A0000A0' + size_text
    output = bytearray(b':' + body + f'{(-sum(body)) & 0xFF:02X}'.encode() + b'\r\n')
    output += HEX_FRAMING
    output += address_record(0xA3, 0)
    high = segment = position = 0
    while position < len(image):
        next_high = position >> 20
        next_segment = (position & 0xF0000) >> 4
        if next_high != high:
            output += address_record(0xA3, next_high)
            high = next_high
        if next_segment != segment:
            output += address_record(0x02, next_segment)
            segment = next_segment
        chunk = image[position:position + min(128, 0x10000 - (position & 0xFFFF))]
        output += data_record(0x84, position & 0xFFFF, chunk)
        position += len(chunk)
    output += b':00000001FF\r\n'
    return bytes(output)


# ---- device tree helpers -----------------------------------------------------

def fdt_chosen(dtb):
    """Return the properties of /chosen from a flattened device tree."""
    magic, _, off_struct, off_strings = struct.unpack_from('>IIII', dtb, 0)
    if magic != 0xD00DFEED:
        raise ValueError('not a device tree blob')
    pos, depth, path, props = off_struct, 0, [], {}
    while True:
        token = struct.unpack_from('>I', dtb, pos)[0]
        pos += 4
        if token == 1:                                   # FDT_BEGIN_NODE
            end = dtb.index(b'\0', pos)
            path.append(dtb[pos:end].decode())
            pos = (end + 4) & ~3
        elif token == 2:                                 # FDT_END_NODE
            path.pop()
        elif token == 3:                                 # FDT_PROP
            length, name_off = struct.unpack_from('>II', dtb, pos)
            pos += 8
            name_end = dtb.index(b'\0', off_strings + name_off)
            if path == ['', 'chosen']:
                props[dtb[off_strings + name_off:name_end].decode()] = dtb[pos:pos + length]
            pos = (pos + length + 3) & ~3
        elif token == 4:                                 # FDT_NOP
            continue
        elif token == 9:                                 # FDT_END
            return props
        else:
            raise ValueError(f'bad device tree token {token}')


# ---- main --------------------------------------------------------------------

def loader_with_checksum(loader):
    """The loader verifies itself: its last word must be the sum of all others."""
    data = bytearray(loader)
    if len(data) % 4:
        raise ValueError('loader size is not a multiple of 4')
    total = sum(struct.unpack_from('<I', data, i)[0] for i in range(0, len(data) - 4, 4))
    struct.pack_into('<I', data, len(data) - 4, total & 0xFFFFFFFF)
    return bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--loader', type=Path, required=True)
    parser.add_argument('--zimage', type=Path, required=True)
    parser.add_argument('--dtb', type=Path, required=True)
    parser.add_argument('--initramfs', type=Path, required=True)
    parser.add_argument('-o', '--output', type=Path, required=True)
    parser.add_argument('--entry-out', type=Path, help='also write loader+blob (for emulate_loader.py)')
    parser.add_argument('--blob-out', type=Path, help='also write the blob alone')
    args = parser.parse_args()

    problems = []
    loader = loader_with_checksum(args.loader.read_bytes())
    zimage, dtb, initrd = (p.read_bytes() for p in (args.zimage, args.dtb, args.initramfs))

    if struct.unpack_from('<I', loader, 0)[0] & 0xFFFFF000 != 0xE59FF000:
        problems.append('loader does not start with a pc-relative "ldr pc"')
    if len(loader) > BLOB_OFFSET - NK_ENTRY:
        problems.append('loader does not fit before the blob')
    if struct.unpack_from('<I', zimage, 0x24)[0] != 0x016F2818:
        problems.append('zImage magic missing')
    if initrd[:2] != b'\x1f\x8b':
        problems.append('initramfs is not gzip-compressed')
    try:
        chosen = fdt_chosen(dtb)
        start = struct.unpack('>I', chosen.get('linux,initrd-start', b'\0' * 4))[0]
        end = struct.unpack('>I', chosen.get('linux,initrd-end', b'\0' * 4))[0]
        bootargs = chosen.get('bootargs', b'').rstrip(b'\0').decode()
        if start != INITRD_ADDR:
            problems.append(f'linux,initrd-start is 0x{start:08X}, expected 0x{INITRD_ADDR:08X}')
        if end != INITRD_ADDR + len(initrd):
            problems.append(f'linux,initrd-end is 0x{end:08X}, expected 0x{INITRD_ADDR + len(initrd):08X}')
        if any(x in bootargs for x in ('earlycon', 'console=ttyS', 'console=ttyO')):
            problems.append('bootargs select a UART console, which hangs this board')
    except ValueError as exc:
        problems.append(f'device tree: {exc}')
        bootargs = '?'
    if problems:
        sys.exit('mkhex: ' + '; '.join(problems))

    blob = struct.pack('<III', len(zimage), len(dtb), len(initrd)) + zimage + dtb + initrd
    total = BLOB_OFFSET + len(blob)
    if total > MAX_IMAGE_SIZE:
        sys.exit(f'mkhex: image is {total / 1e6:.1f} MB (zImage {len(zimage) / 1e6:.1f}, initramfs '
                 f'{len(initrd) / 1e6:.1f}); the limit is {MAX_IMAGE_SIZE / 1e6:.1f} MB, the part '
                 'of the OS area the monitor loads.')
    entry = loader + bytes(BLOB_OFFSET - NK_ENTRY - len(loader)) + blob
    image = image_prefix() + entry
    output = build_hex(image)

    # Read the result back the way the monitor would.
    offset = 0
    while offset < len(output):
        *_, offset = parse_record(output, offset)
    if decode_hex(output) != image or image[NK_ENTRY:] != entry:
        sys.exit('mkhex: the finished HEX does not decode to the intended image')

    args.output.write_bytes(output)
    if args.entry_out:
        args.entry_out.write_bytes(entry)
    if args.blob_out:
        args.blob_out.write_bytes(blob)
    print(f'mkhex: loader {len(loader)} B, zImage {len(zimage)} B, dtb {len(dtb)} B, '
          f'initramfs {len(initrd)} B')
    print(f'mkhex: bootargs "{bootargs}"')
    print(f'mkhex: {args.output} ({len(output)} bytes, image {len(image)} bytes) '
          f'sha256 {hashlib.sha256(output).hexdigest()}')


if __name__ == '__main__':
    main()
