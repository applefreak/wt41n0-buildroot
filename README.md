# Linux for the Zebra / Symbol WT41N0

A [Buildroot](https://buildroot.org) external tree (`BR2_EXTERNAL`) that builds a
complete, small Linux system for the WT41N0 wearable terminal (TI OMAP4430) and
packages it as an image the unit's own bootloader accepts over USB.

The system runs from RAM, or from an ext4 root file system on the eMMC once installed
there. It replaces the Windows CE image in the unit's OS area and leaves the bootloader
alone, so flashing the stock OS image again restores the unit.

## Hardware Status

| Hardware | Status |
|---|---|
| Display, backlight | 320x240 LCD console, PWM brightness in `/sys/class/backlight` |
| Keypad | all keys, with the orange / gray / blue layers on the text console (`keys` shows them); its backlight (`/sys/class/leds/keypad-backlight`) automatic, always on or off, set in Settings |
| Touch screen | resistive panel as an input device (units without one are fine) |
| LEDs, power button | the three body LEDs in `/sys/class/leds`; the power button opens the Power app and wakes the unit from suspend (`KEY_POWER`) |
| CPU, power | 300 to 800 MHz by load; suspend (suspend-to-idle, `wt41n0-suspend`), woken by the power button; power-off, after which the cradle or a battery reinsertion starts the unit again (the power button cannot) |
| Wi-Fi | WL1283, WPA2, 2.4 and 5 GHz; SSH server |
| Bluetooth | classic Bluetooth (BR/EDR): scan, discoverable, pairing with `bluetoothctl`; no Low Energy. Needs the chip's init script from your unit's Windows CE image (see `local/README.md`) |
| USB | serial console over the cradle's USB connection (full speed) |
| Audio | built-in speaker through ALSA (`aplay file.wav`); built-in microphone records, but at the wrong rate for now |
| Battery, charging | battery voltage, capacity, current and temperature in `/sys/class/power_supply`; charges in the cradle, with the middle LED blinking orange |
| Ring scanner | barcodes from the ring scanner with its trigger (`wt41n0-scan`); the accessory ports also work as a plain serial port (`wt41n0-fpga serial on`, `/dev/ttyS1`) |
| Launcher | an LVGL home screen on the LCD with app tiles, Settings (Wi-Fi, display, sound, theme, text input, time zone, touch calibration), Scanner, System (device IDs, hardware and live sensor readings) and Power; four visual styles to choose from |
| Sensors, storage | accelerometer (IIO), PMIC ADC, eMMC root file system that keeps every change (`support/install.py`) |

Not working yet: Bluetooth Low Energy, headset audio, the hardware beeper, deep sleep (suspend works as suspend-to-idle).

## What you need

- A Linux machine or container to build on (Buildroot does not build on macOS; see
  "Building in a container" below), with Buildroot's usual host packages and `python3`.
- To flash: the cradle/dock, and Python with `pyusb`.

## Build

```sh
git clone --branch 2025.02.x https://gitlab.com/buildroot.org/buildroot.git
git clone <this repository> wt41n0

cd buildroot
make BR2_EXTERNAL=../wt41n0 wt41n0_defconfig
make
```

The images are in `output/images/`: `wt41n0-install.hex` (installs on the eMMC),
`wt41n0-emmc.hex` (updates an installed unit) and `wt41n0-linux.hex` (runs from RAM).

If you wish to include your SSH public key and/or Wi-Fi config in the image, `local/`
can hold a `wifi.conf`, your `authorized_keys` and a fixed host key, built in
with `BR2_WT41N0_LOCAL_WIFI` and `BR2_WT41N0_LOCAL_SSH` (see `local/README.md`).

## Flash

There are three ways to flash the image: Zebra's official **USBDownload** tool, the
`support/flash.py` script, or the **Web Flasher**.

Whichever way you choose, first put the unit in USB download mode with the IPL (initial
program loader):

1. During a cold boot, when the screen shows three braces `}}}`, press **P2** and
   choose **Download from USB**.
2. Put the unit in the cradle, connected to the computer over USB, then:
   1. With **USBDownload**: open `wt41n0-install.hex` with it, then click **Load**.
   2. With the Python script: run `support/flash.py output/images/wt41n0-install.hex`.
   3. With the **Web Flasher**: visit the
      [Web Flasher](https://applefreak.github.io/wt41n0-buildroot/) and follow the
      instructions there.
3. When the transfer finishes, exit the IPL following the on-screen instructions.

This installs Linux on the eMMC, erasing Windows CE completely. If you wish to keep the
original `\Application` and `\Platform` partitions, use `wt41n0-linux.hex` instead: it
runs from RAM, nothing is kept between boots, and .

**Updating a running unit.** Once the unit runs this system and is reachable over SSH,
a new build can be installed without download mode. On a unit installed on the eMMC:

```sh
support/update.py output/images/wt41n0-emmc.hex --host root@<address>
```

Use `wt41n0-linux.hex` instead for a unit running from RAM; on a unit installed on the
eMMC, it switches back to the RAM system. This updates the kernel and the boot image
only; the eMMC root file system keeps its files and settings (see `docs/customizing.md`).

To return to Windows CE, flash Zebra's stock OS update for the WT41N0
(`41X0c70BenOS055702.hex`, from Zebra's support site) the same way. Get that file
before you start, so you always have a way back.

## Use

The unit boots to the launcher on its LCD, driven by the keypad (or the touch screen):
app tiles, numbered 1 to 9. The Terminal app is a root shell; the power button opens
the Power app. Easier ways in:

- **SSH**: `ssh root@<address>`. The address is shown on the LCD when Wi-Fi connects.
- **USB serial**: the unit appears as a serial port on the host.

The launcher's settings live in `/etc/wt41n0/ui.conf` (`style=`, `palette=`,
`accent=`, where palette and accent can be `auto` for the style's defaults; also
`brightness=`, `volume=`, `tz=`, `input=`, `touch_cal=`). Apps are listed by the manifests
in `/usr/share/wt41n0/apps`.

## Customize

Everything is ordinary Buildroot: `make menuconfig` for packages,
`make linux-menuconfig` for the kernel. Board-specific settings are under
**External options**:

- **Boot verbosity**: `verbose` (the default: kernel and start-up messages scroll
  on the LCD), `quiet` (the loader's progress screen, then the prompt), or `debug` (loader diagnostic screen and the kernel's
  LCD debug console from its first instruction).
- **The `local/` directory** for your private files.

SSH logs in as root with the password **`wt41n0`**; the first login asks for a new password
and ends the session if it is not changed. The consoles (the Terminal app and USB) are
root shells without a login. Your own SSH key and a fixed host key can be built in
instead (`BR2_WT41N0_LOCAL_SSH`, see `docs/customizing.md`).

## Layout

```
configs/wt41n0_defconfig          the Buildroot configuration
board/zebra/wt41n0/               device tree, kernel patches and config fragments,
                                  root filesystem overlay, post-build/post-image scripts
package/wt41n0-loader/            loader the bootloader jumps to
package/wt41n0-utils/             small board tools (pcmtest, keys, ...)
package/wt41n0-ui/                the launcher, its apps and their LVGL library
support/                          host tools: mkhex.py, flash.py, console.py, keymap.py,
                                  flasher/
docs/                             hardware notes and how the boot works
local/                            your private files: Wi-Fi config, SSH keys (not tracked)
```

## Prebuilt images

Images built by GitHub are on the
[**Releases**](https://github.com/applefreak/wt41n0-buildroot/releases) page. They have
Bluetooth disabled (see the Bluetooth row above).

## Building in a container

If you're building on a non-Linux machine, the `build-in-container.sh` script clones
Buildroot, loads the defconfig, builds, and copies the images out. In any Debian or Ubuntu container with this tree mounted:

```sh
docker exec <container> bash -lc '/path/to/wt41n0/build-in-container.sh'
```
