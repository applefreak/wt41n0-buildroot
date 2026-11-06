# Linux for the Zebra / Symbol WT41N0

A [Buildroot](https://buildroot.org) external tree (`BR2_EXTERNAL`) that builds a
complete, small Linux system for the WT41N0 wearable terminal (TI OMAP4430) and
packages it as an image the unit's own bootloader accepts over USB.

The system runs from RAM. It replaces the Windows CE image in the unit's OS area and
leaves the bootloader and the other storage areas alone, so flashing the stock OS
image again restores the unit. The image is built entirely from this tree: no Zebra
file is needed to build it.

## What works

| | |
|---|---|
| Display, backlight | 320x240 LCD console, PWM brightness in `/sys/class/backlight` |
| Keypad | all keys, with the orange / gray / blue layers on the text console (`keys` shows them) |
| Touch screen | resistive panel as an input device (units without one are fine) |
| LEDs, power button | the three body LEDs in `/sys/class/leds`; power button as a `KEY_POWER` event |
| Wi-Fi | WL1283, WPA2, 2.4 and 5 GHz; ssh server |
| Bluetooth | classic Bluetooth (BR/EDR): scan, discoverable, pairing with `bluetoothctl`; no Low Energy. Needs the chip's init script from your unit's Windows CE image (see `local/README.md`) |
| USB | serial console over the USB / cradle cable (full speed) |
| Audio | built-in speaker through ALSA (`aplay file.wav`); built-in microphone records, but at the wrong rate for now |
| Sensors, storage | accelerometer (IIO), PMIC ADC, eMMC (read access) |

Not working yet: Bluetooth Low Energy, headset audio, battery and charger reporting, the
hardware beeper, the ring-scanner port, suspend.

## What you need

- A Linux machine or container to build on (Buildroot does not build on macOS; see
  "Building in a container" below), with Buildroot's usual host packages and `python3`.
- To flash: a USB cable or cradle, and Python with `pyusb`.

## Build

```sh
git clone --branch 2025.02.x https://gitlab.com/buildroot.org/buildroot.git
git clone <this repository> wt41n0

cd buildroot
make BR2_EXTERNAL=../wt41n0 wt41n0_defconfig
make
```

The result is `output/images/wt41n0-linux.hex`.

Optional, before building: put a `wifi.conf` and your ssh `authorized_keys` in
`local/` (see `local/README.md`). Nothing in `local/` is tracked by git.

## Flash

1. Put the unit in download mode: hold **P1** or **P2** while switching it on and
   choose **Download from USB**.
2. Connect it over USB and run `support/flash.py output/images/wt41n0-linux.hex`.
3. When the transfer finishes, power-cycle the unit.

Without Python: `support/flasher/index.html` does the same in Chrome or Edge over
WebUSB. Serve it locally (`cd support/flasher && python3 -m http.server 8000`, then open
`http://localhost:8000`) or from any HTTPS host, choose the HEX file and click **Connect
and flash**. On Windows, bind the WinUSB driver to the unit (`05e0:200b`) with Zadig first.

**Installing on the eMMC.** The system can also run from the eMMC, with an ext4 root
file system that keeps every change (`support/install.py`, see `docs/customizing.md`).
This replaces the Windows CE data partitions.

**Updating a running unit.** Once the unit runs this system and is reachable over ssh,
a new build can be installed without download mode:

```sh
support/update.py output/images/wt41n0-linux.hex --host root@<address>
```

It writes the image to the same eMMC area download mode uses, verifies it and reboots.
If an image does not boot, download mode and `flash.py` still work as before.

To return to Windows CE, flash Zebra's stock OS update for the WT41N0
(`41X0c70BenOS055702.hex`, from Zebra's support site) the same way. Get that file
before you start, so you always have a way back.

## Use

The unit boots to a root shell on its LCD, driven by the keypad. Easier ways in:

- **ssh**: `ssh root@<address>`. The address is shown on the LCD when Wi-Fi connects.
- **USB serial**: the unit appears as a serial port on the host
  (`screen /dev/ttyACM0 115200`, or `support/console.py '<command>'`).

## Customize

Everything is ordinary Buildroot: `make menuconfig` for packages,
`make linux-menuconfig` for the kernel. Board-specific settings are under
**External options**:

- **Boot verbosity**: `verbose` (the default: kernel and start-up messages scroll
  on the LCD), `quiet` (the loader's progress screen, then the prompt), or `debug` (loader diagnostic screen and the kernel's
  LCD debug console from its first instruction).
- **The `local/` directory** for your private files.

Defaults are developer-friendly: the consoles are root shells without a login, ssh
accepts keys, and password logins work only once you set a root password
(`BR2_TARGET_GENERIC_ROOT_PASSWD`). Tighten this before using a unit anywhere you do
not control.

## Layout

```
configs/wt41n0_defconfig          the Buildroot configuration
board/zebra/wt41n0/               device tree, kernel patches and config fragments,
                                  root filesystem overlay, post-build/post-image scripts
package/wt41n0-loader/            loader the bootloader jumps to
package/wt41n0-utils/             small board tools (pcmtest, keys, ...)
support/                          host tools: mkhex.py, flash.py, console.py, keymap.py,
                                  flasher/ (the same flashing as a WebUSB web page)
docs/                             hardware notes and how the boot works
local/                            your private files: Wi-Fi config, ssh keys (not tracked)
```

## Building in a container

`build-in-container.sh` clones Buildroot, loads the defconfig, builds, and copies the
images out. In any Debian or Ubuntu container with this tree mounted:

```sh
docker exec <container> bash -lc '/path/to/wt41n0/build-in-container.sh'
```
