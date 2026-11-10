# Debugging

## Getting a shell without the keypad

- **USB serial**: in the cradle, connected over USB, the unit shows up as a
  serial port a few seconds after boot. `support/console.py 'dmesg | tail'` runs one
  command; any terminal program works for an interactive session. No network needed.
- **ssh**: `ssh root@<address>` once Wi-Fi is configured.

## Boot verbosity

Set "boot verbosity" under External options and rebuild.

| Profile | Loader | Kernel | Start-up scripts |
|---|---|---|---|
| verbose (default) | progress lines | `loglevel=7`: messages scroll on the LCD | every line on the LCD |
| quiet | progress lines, kept until the prompt | `quiet loglevel=3`: errors only | to `/var/log/boot.log`; failures shown on the LCD |
| debug | diagnostic screen: image sizes, register dump, a colour per copy stage | LCD low-level console from the first instruction (`earlyprintk keep_bootcon ignore_loglevel`) | every line on the LCD |

Once start-up has finished, only kernel warnings and errors reach the LCD, so they do
not interrupt the shell (`CONSOLE_LOGLEVEL` in `/etc/default/wt41n0`; `dmesg -n 8`
shows everything again). `dmesg` and `/var/log/boot.log` have everything regardless of
the profile.

The unit has no reachable UART. Do not add `console=ttyS...` or `earlycon` to the
kernel command line: the early UART console waits forever and the unit appears dead.
`mkhex.py` rejects such a command line.

## When the unit does not boot

1. Read the screen. A coloured screen with a message is the loader refusing the image
   ("BAD KERNEL START", "BAD DTB", ...) or a CPU exception in the loader. An orange
   screen with no text means the loader's self-check failed (corrupt transfer).
2. Build the `debug` profile. The loader then shows what it loaded, and the kernel
   prints on the LCD from the decompressor on, so you see how far it gets.
3. Flash a known-good image, or the stock HEX. Download mode is in the bootloader and
   does not depend on the OS image, so a bad image cannot lock you out.

## Known rough edges

- `cpuidle.off=1` is on the command line: there is no broadcast clock event device yet,
  and the deep idle states lose timers without one.
- All PMIC rails are forced on; the unit draws more than it would under the stock OS.
- High-speed USB enumeration fails on the device port, so it is limited to full speed.
- Access points that never answer authentication make wpa_supplicant ignore all access
  points for longer and longer; `S40wifi` keeps its ignore list empty while the link is down.
- Never unbind or rebind the Bluetooth driver (`hci-ti`) on a running system: it resets
  the Bluetooth half of the WL1283, which crashes the Wi-Fi firmware too. Reboot instead.
- Reading I2C address `0x6c` on the PMIC bus (`i2c-0`) hangs the unit. Leave it alone.
- tinyalsa's `tinyplay` and `tinycap` fail silently on this sound hardware. Use `aplay`,
  `arecord` or `pcmtest`.

## Hardware state

`wt41n0-survey` collects logs and the state of GPIO, pins, clocks, regulators, I2C buses,
network, Bluetooth and sound into `/tmp/wt41n0-survey/` and a tar archive, read-only.
`wt41n0-fpga` prints the FPGA's registers.
