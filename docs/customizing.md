# Customizing the image

The tree is a normal Buildroot external tree. `make menuconfig` changes the package
selection, `make linux-menuconfig` the kernel, `make busybox-menuconfig` BusyBox.
`make savedefconfig` writes your Buildroot configuration back to
`configs/wt41n0_defconfig`.

## Board options (menuconfig: External options)

| Option | Meaning |
|---|---|
| directory with local, private files | default `local/` |
| random seed in the device tree | on by default: the kernel's random generator is ready at boot, so ssh answers right away (without it, about a minute) |
| boot verbosity | `quiet`, `verbose` or `debug` (see `debugging.md`) |

## Wi-Fi and ssh

Put these in `local/` and rebuild (`make` is enough):

- `wifi.conf`: a `wpa_supplicant.conf`. Installed as `/etc/wpa_supplicant.conf`.
  Without it Wi-Fi stays off.
- `authorized_keys`: public keys for root.
- `dropbear_ed25519_host_key`: a fixed host key. The system runs from RAM, so without
  one the unit gets a new ssh identity at every boot.

Password logins over ssh work only when root has a password: set
`BR2_TARGET_GENERIC_ROOT_PASSWD` (System configuration, Root password).

For proper regulatory limits add `country=XX` to `wifi.conf`.

## Console access

`/etc/inittab` starts a root shell without login on the LCD (`tty1`) and on the USB
serial port. To require a login, replace those lines with `getty` lines, for example:

```
tty1::respawn:/sbin/getty -L tty1 0 linux
```

and set a root password. Put your own `inittab` in a root filesystem overlay
(`BR2_ROOTFS_OVERLAY` accepts several directories) rather than editing the board's.

## Start-up scripts and settings

Services are ordinary `/etc/init.d/S*` scripts:

| Script | Does |
|---|---|
| `S10keymap` | loads the keypad console keymap |
| `S20audio` | routes playback to the built-in speaker, sets the volume |
| `S40wifi` | wpa_supplicant + DHCP on `wlan0`, in the background |
| `S50dropbear` | ssh server (from the dropbear package) |

`/etc/default/wt41n0` holds the speaker volume, whether the address is printed on the
LCD when Wi-Fi connects, and which kernel messages still reach the LCD after start-up.

## Bluetooth

Classic Bluetooth (BR/EDR) through BlueZ: `bluetoothd` starts at boot, `bluetoothctl`
controls it, for example:

```sh
bluetoothctl power on
bluetoothctl --timeout 15 scan on
bluetoothctl discoverable on
```

Pairing needs an agent; in an interactive `bluetoothctl`, `agent NoInputNoOutput` and
`default-agent` accept "just works" pairing. Low Energy is not available: the controller
advertises it but refuses LE commands, so the kernel (patch 0009) uses it as BR/EDR
only. The init script has to come from your unit's Windows CE image
(`local/README.md`).

## Keypad

All layers come from one table in `support/keymap.py`: the matrix positions go into
the device tree (`--dts`), the layers into the console keymap (`--bmap`, done by the
build). Edit the table and rebuild. On the unit, `keys` prints a summary.

## Audio

`aplay`, `arecord` and `amixer` work through `/etc/asound.conf`, which converts any
format to what the hardware accepts and routes stereo to the built-in speaker
(`default`); `-D headset` selects the headset outputs, which are untested. The speaker volume is the mixer control
"Handsfree Playback Volume" (0 to 29). `pcmtest` plays a test tone without the ALSA
library.

## LEDs and backlight

```sh
echo 1 > /sys/class/leds/wt41n0:green:right/brightness
echo timer > /sys/class/leds/wt41n0:red:middle/trigger
echo 10 > /sys/class/backlight/backlight/brightness      # 0..17
```

## Kernel

`board/zebra/wt41n0/linux.fragment` is applied on top of `omap2plus_defconfig`, and
`patches/linux/` holds the board's kernel patches. After changing the device tree, a
patch or the fragment: `make linux-rebuild all` (a changed patch needs
`make linux-dirclean all`).
