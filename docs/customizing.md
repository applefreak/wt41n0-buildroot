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

## Installing on the eMMC

By default the system runs from RAM and nothing persists. It can also be installed on
the eMMC, where it runs from an ext4 root file system and keeps every change. Two ways:

- **In download mode:** flash `output/images/wt41n0-install.hex` like the RAM image and
  power-cycle. At its first boot it creates the root file system on the eMMC, copies
  itself into it and boots it; no network is needed. It installs only when the root
  partition holds no ext2/3/4 file system yet (a unit coming from Windows CE), so
  flashing it again never overwrites an installed system.
- **Over ssh:** `support/install.py output/images --host root@<address>`. The unit has
  to run this system (from RAM or already from the eMMC). The script writes
  `rootfs.ext4.gz` to the eMMC from 201 MiB to the end, installs the eMMC boot image
  `wt41n0-emmc.hex` into the OS area and reboots. On the first boot `S05growfs` grows
  the 128 MiB file system to its 1687 MiB partition. This also replaces an installed
  system.

**This erases the Windows CE Application and Platform partitions.** The boot chain, the
OS area and Application's first sector (which the bootloader checks) are not touched.
See `hardware.md` ("Storage") for the layout.

**The eMMC boot image is also the recovery system.** It carries the complete RAM system;
its `/init` (`board/zebra/wt41n0/emmc-init`) switches to the eMMC root unless:

- **ESC is held** while the unit starts (not working yet: the key is not seen at that
  point of the boot),
- `/boot/recovery-once` exists on the eMMC root (it is removed when used), or
- the eMMC root is missing or cannot be mounted.

The recovery system is the RAM system, with Wi-Fi, ssh and the installer.

- **Updating the kernel:** `support/update.py output/images/wt41n0-emmc.hex`. It
  writes only the OS area.
- **Updating the root file system:** run `install.py` again. It reboots into the
  recovery system, rewrites the partition (your changes on the unit are lost) and
  reboots into the new root.
- **Back to the RAM-only system:** `support/update.py output/images/wt41n0-linux.hex`,
  or download mode with `flash.py`. The eMMC root stays as it is.
- **Back to Windows CE:** flash the stock OS in download mode, then Zebra's
  `CleanAppl.hex` and `CleanPlat.hex` from its OS update package. Windows CE then
  formats fresh Application and Platform partitions on its first boot.

## Wi-Fi and ssh

By default the image has neither: Wi-Fi networks are added on the unit (Settings,
Wi-Fi) and ssh logs in as root with the password `wt41n0` (`BR2_TARGET_GENERIC_ROOT_PASSWD`
in the defconfig), which the first ssh login must change
(`BR2_WT41N0_ROOT_PASSWD_EXPIRED`; the session ends otherwise). Each unit makes its own
ssh host key at its first start.

For your own development images, put these in `local/`, turn on the options (External
options in `make menuconfig`) and rebuild (`make` is enough):

- `wifi.conf`: a `wpa_supplicant.conf`, installed as `/etc/wpa_supplicant.conf` only
  when `BR2_WT41N0_LOCAL_WIFI` is on (External options, "build local/wifi.conf into
  the image"). It is off by default: the image then has no networks, and you add them
  on the unit in Settings, Wi-Fi, where they are saved.
- `authorized_keys`: public keys for root (with `BR2_WT41N0_LOCAL_SSH`).
- `dropbear_ed25519_host_key`: a fixed host key (with `BR2_WT41N0_LOCAL_SSH`). Without
  one each unit makes its own; a system running from RAM gets a new one at every boot.

To build without a root password, clear `BR2_TARGET_GENERIC_ROOT_PASSWD` (System
configuration, Root password); ssh then accepts only keys, since dropbear refuses empty
passwords.

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
