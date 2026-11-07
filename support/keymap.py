#!/usr/bin/env python3
"""WT41N0 keypad: one table -> device-tree linux,keymap + BusyBox console keymap.

KEYS below maps each key to its (row, column) in the OMAP4 keypad matrix and to
its symbol on each layer. The layers live in the Linux console keymap and follow
the key legends:
  plain                       digits, backspace (SPACE/BKSP key), tab, esc, enter,
                              up/down, / . -
  big ORANGE (sticky AltGr)   orange letters a c e g i k m o q s u w y (next key;
                              press twice to lock, once more to unlock)
  big GRAY (sticky Shift)     gray letters   b d f h j l n p r t v x z (same)
  ORANGE + GRAY               punctuation !@#$%^&*() = [ ] _ | ,
  BLUE (sticky Alt, next key) space, F1..F10, left/right
  ALT/CTRL held               Ctrl

Usage: support/keymap.py --dts       print the linux,keymap entries for the device tree
       support/keymap.py --bmap      write the binary console keymap (loadkmap format)
                                     to stdout; the build installs it as /etc/wt41n0.bmap
       support/keymap.py --c-header  print the layer table as C, for the wt41n0-ui input
                                     code (package/wt41n0-ui/src/lib/keymap.h)
"""
import struct
import sys

# label: (row, col, linux keycode, plain, gray, orange, orange+gray, blue)
KEYS = {
    'ESC':        (3, 7,   1, 'Esc',   'Esc',   'Esc',   'Esc',   'Esc'),
    'MENU_TAB':   (2, 7,  15, 'Tab',   '*',     '-',     '_',     'Tab'),
    'ALT_CTRL':   (1, 7,  29, 'Ctrl',  'Ctrl',  'Ctrl',  'Ctrl',  'Ctrl'),
    'SHIFT':      (0, 7,  42, 'Shift', 'Shift', 'Shift', 'Shift', 'Shift'),
    'BLUE':       (3, 6,  56, 'SAlt',  'SAlt',  'SAlt',  'SAlt',  'SAlt'),
    'K1_AB':      (0, 0,   2, '1', 'b', 'a', '!', 'F1'),
    'K2_CD':      (2, 2,   3, '2', 'd', 'c', '@', 'F2'),
    'K3_EF':      (1, 2,   4, '3', 'f', 'e', '#', 'F3'),
    'K4_GH':      (3, 5,   5, '4', 'h', 'g', '$', 'F4'),
    'K5_IJ':      (3, 4,   6, '5', 'j', 'i', '%', 'F5'),
    'K6_KL':      (3, 3,   7, '6', 'l', 'k', '^', 'F6'),
    'K7_MN':      (2, 5,   8, '7', 'n', 'm', '&', 'F7'),
    'K8_OP':      (2, 4,   9, '8', 'p', 'o', '*', 'F8'),
    'K9_QR':      (1, 0,  10, '9', 'r', 'q', '(', 'F9'),
    'SPACE_ST':   (1, 5,  14, 'BackSpace', 't', 's', '-', ' '),
    'K0_UV':      (1, 4,  11, '0', 'v', 'u', ')', 'F10'),
    'ARROW_WX':   (1, 3, 103, 'Up',   'x', 'w', '[', 'Left'),
    'ARROW_YZ':   (0, 3, 108, 'Down', 'z', 'y', ']', 'Right'),
    'BIG_ORANGE': (0, 5, 100, 'SAltGr', 'SAltGr', 'SAltGr', 'SAltGr', 'SAltGr'),
    'BIG_GRAY':   (0, 4,  58, 'SShift', 'SShift', 'SShift', 'SShift', 'SShift'),
    'P1':         (2, 6,  53, '/', '|', '/', '\\', '/'),
    'P2':         (1, 6,  52, '.', ',', '.', '=', '.'),
    'ENTER':      (0, 6,  28, 'Enter', 'Enter', 'Enter', 'Enter', 'Enter'),
}

# Linux keymap symbol encoding: (type << 8) | value
SPECIAL = {
    'Esc': 0x001b, 'Tab': 0x0009, 'BackSpace': 0x007f, 'Enter': 0x0201,
    'Up': 0x0603, 'Down': 0x0600, 'Left': 0x0601, 'Right': 0x0602,
    'Shift': 0x0700, 'AltGr': 0x0701, 'Ctrl': 0x0702, 'Alt': 0x0703,
    'Shift_Lock': 0x0a00, 'AltGr_Lock': 0x0a01,
    # KT_SLOCK sticky modifiers: apply to the next key only; press twice to lock
    'SShift': 0x0c00, 'SAltGr': 0x0c01, 'SAlt': 0x0c03,   # KT_SLOCK = 12
    'F1': 0x0100, 'F2': 0x0101, 'F3': 0x0102, 'F4': 0x0103, 'F5': 0x0104,
    'F6': 0x0105, 'F7': 0x0106, 'F8': 0x0107, 'F9': 0x0108, 'F10': 0x0109,
}


def symbol(name, ctrl):
    if name is None:
        return 0x0200          # VoidSymbol
    if name in SPECIAL:
        return SPECIAL[name]
    ch = ord(name)
    if ctrl and name.isalpha():
        return ch & 0x1f       # Control_x
    return ch


def dts():
    lines = []
    for label, (row, col, code, *_rest) in KEYS.items():
        lines.append(f'\t\tMATRIX_KEY({row}, {col}, {code})\t/* {label} */')
    return '\n'.join(lines)


def bmap():
    NR_KEYS, MAX_NR_KEYMAPS = 128, 256
    maps = {}
    for m in range(16):            # shift=1 altgr=2 ctrl=4 alt=8
        shift, altgr, ctrl, alt = m & 1, m & 2, m & 4, m & 8
        table = [0x0200] * NR_KEYS
        for label, (row, col, code, plain, gray, orange, both, blue) in KEYS.items():
            if alt:
                name = blue
            elif altgr and shift:
                name = both
            elif altgr:
                name = orange
            elif shift:
                name = gray
            else:
                name = plain
            table[code] = symbol(name, ctrl)
        maps[m] = table
    out = b'bkeymap'
    out += bytes(1 if i in maps else 0 for i in range(MAX_NR_KEYMAPS))
    for i in range(MAX_NR_KEYMAPS):
        if i in maps:
            out += b''.join(struct.pack('<H', v) for v in maps[i])
    return out


# Symbols other than single characters, for --c-header
C_SYMBOLS = {
    'Esc': 'UI_SYM_ESC', 'Tab': 'UI_SYM_TAB', 'BackSpace': 'UI_SYM_BACKSPACE',
    'Enter': 'UI_SYM_ENTER', 'Up': 'UI_SYM_UP', 'Down': 'UI_SYM_DOWN', 'Left': 'UI_SYM_LEFT',
    'Right': 'UI_SYM_RIGHT', 'Ctrl': 'UI_SYM_CTRL', 'Shift': 'UI_SYM_SHIFT',
    'SShift': 'UI_SYM_GRAY', 'SAltGr': 'UI_SYM_ORANGE', 'SAlt': 'UI_SYM_BLUE',
    **{f'F{n}': f'UI_SYM_F({n})' for n in range(1, 11)},
}


def c_header():
    def sym(name):
        if name in C_SYMBOLS:
            return C_SYMBOLS[name]
        return "'\\\\'" if name == '\\' else f"'{name}'"
    lines = [
        '/* Generated by support/keymap.py --c-header: the keypad layers. Do not edit. */',
        '#ifndef UI_KEYMAP_H', '#define UI_KEYMAP_H', '',
        '/* Non-character symbols are negative; characters are their ASCII code. */',
        'enum {', '\tUI_SYM_ESC = -1, UI_SYM_TAB = -2, UI_SYM_BACKSPACE = -3, UI_SYM_ENTER = -4,',
        '\tUI_SYM_UP = -5, UI_SYM_DOWN = -6, UI_SYM_LEFT = -7, UI_SYM_RIGHT = -8,',
        '\tUI_SYM_CTRL = -9, UI_SYM_SHIFT = -10,',
        '\tUI_SYM_GRAY = -11, UI_SYM_ORANGE = -12, UI_SYM_BLUE = -13,  /* sticky layer keys */',
        '};', '#define UI_SYM_F(n) (-100 - (n))', '',
        '/* Layers, in the order of the table columns */',
        'enum { UI_LAYER_PLAIN, UI_LAYER_GRAY, UI_LAYER_ORANGE, UI_LAYER_BOTH, UI_LAYER_BLUE, UI_LAYERS };',
        '', 'static const struct ui_key {', '\tunsigned short code;\t/* Linux input event code */',
        '\tconst char *label;', '\tint sym[UI_LAYERS];', '} ui_keymap[] = {']
    for label, (row, col, code, plain, gray, orange, both, blue) in KEYS.items():
        syms = ', '.join(sym(n) for n in (plain, gray, orange, both, blue))
        lines.append(f'\t{{ {code:3}, "{label}", {{ {syms} }} }},')
    lines += ['};', '', '#endif']
    return '\n'.join(lines)


if __name__ == '__main__':
    if '--dts' in sys.argv:
        print(dts())
    elif '--c-header' in sys.argv:
        print(c_header())
    elif '--bmap' in sys.argv:
        sys.stdout.buffer.write(bmap())
    else:
        sys.exit(__doc__)
