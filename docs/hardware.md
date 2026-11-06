# WT41N0 hardware reference

What is known about the Zebra / Symbol WT41N0 hardware and how Linux uses it. Register
offsets are relative to the block base given in each section. GPIO numbers are OMAP4
GPIO numbers (bank = number / 32 + 1, line = number % 32).

## Summary

| Part | Device | Linux driver | Status |
|---|---|---|---|
| SoC | TI OMAP4430 ES2.3, 2 x Cortex-A9, GP (unsigned) | - | working |
| RAM | 512 MiB LPDDR2 (package on package) | - | working |
| Storage | 4 GB eMMC (Toshiba, `004G60`) on MMC2, 8-bit | `omap_hsmmc` | working (read; Symbol layout, no partition table) |
| Display | 240x320 RGB565 LCD, set up by the bootloader | `simple-framebuffer` + fbcon | working |
| Backlight | timer 9 PWM | `pwm-backlight` | working |
| PMIC | TI TWL6030 on I2C1 0x48-0x4A | `twl-core`, `twl6030-regulator`, GPADC, RTC | working |
| Audio codec | TI TWL6040 on I2C1 0x4B, McPDM | `omap-abe-twl6040` | speaker working; microphone partly |
| Keypad | OMAP4 keypad controller, 8x8 matrix | `omap4-keypad` | working |
| Touch | Semtech SX8650 resistive, I2C4 0x49 | `sx8654` | working (touch units only) |
| FPGA | Actel IGLOO+ on GPMC CS1 | `wt41n0-fpga` | working (interrupts, outputs) |
| Microcontroller | Atmel ATxmega16A4 "External Agent" on I2C4 0x42 | `leds-wt41n0-ea` | LEDs working |
| Wi-Fi | TI WL1283 on MMC4 (SDIO 0097:4076) | `wlcore`, `wl12xx` | working, 2.4 and 5 GHz |
| Bluetooth | TI WL1283 on UART1 | `hci_ll` | classic (BR/EDR) working; no LE |
| Accelerometer | NXP/Freescale MMA8452Q on I2C2 0x1C | `mma8452` | working |
| USB | MUSB OTG, device mode | `musb-hdrc`, `g_serial` | working at full speed only |
| Power button | TWL6030 PWRON | `twl6030-pwrbutton` | working (key events) |
| Scanner port | UART2 | - | not supported yet |

## Boot state

The board is started by the Symbol bootloader, which leaves the hardware configured; the
device tree relies on that (see `boot-flow.md`):

- Pin multiplexing, DDR and clocks are set up; almost no pinmux entries are needed.
- The display controller is running with the framebuffer at `0x9B000000`, 240x320 RGB565,
  480-byte stride. The panel is mounted rotated: `fbcon=rotate:3`. The display controller
  must not be probed by Linux; resetting it blanks the panel.
- The PL310 L2 cache is left enabled; the loader disables it before starting Linux.
- PMIC rails are left as listed under "Power". All are kept on.

### GPIO outputs driven by the bootloader

The kernel resets the GPIO blocks, so these are restored with `gpio-hog` nodes:

| GPIO | Level | Function |
|---|---|---|
| 11, 12 | high | unknown |
| 13 | low | unknown |
| **41** | high | **FPGA enable**: low holds the FPGA in reset (which also blanks the LCD) |
| 48 | low | unknown |
| 81, 83 | low | unknown |
| 122 | high | backlight full on; the pad is muxed to the PWM instead |
| 163, 184 | high | unknown |
| 187, 188 | low | unknown |

Other GPIOs in use: 9 FPGA interrupt (rising edge), 10 External Agent interrupt, 45 Wi-Fi
interrupt, 100 Wi-Fi enable, 103 Bluetooth enable, 104 audio codec power (`audpwron`),
139 Bluetooth host wake.

## Power

- **TWL6030** on I2C1 (0x48, 0x49, 0x4A), interrupt SPI 7.
- Rails as the bootloader leaves them: VANA, VAUX1 (3.0 V, eMMC), VAUX2 (1.8 V), VMMC,
  VPP, VUSIM on; **VAUX3 off**, programmed to 2.8 V, consumer unknown (its selector value
  is outside the Linux driver's table, so the node is disabled). One of the rails feeds the
  LCD panel; switching unused rails off blanks it, so all are marked `regulator-always-on`.
- 32 kHz outputs: CLK32KAUDIO must be on for the TWL6040 (clock 1 of the `twl` node).
  On the TWL6030 a resource needs its power-group register set (`0x07`) before it obeys
  state commands.
- GPADC channels are available in IIO.
- **Do not access I2C1 address 0x6C**: reading its register 2 hangs the unit. The chip is
  unidentified. Nothing answers at 0x6A, where the stock OS configuration lists a BQ24156
  charger.

## Display and backlight

- Backlight: general-purpose timer 9 (`0x4803E000`) in PWM mode from the 38.4 MHz system
  clock, 180 Hz, high time = brightness. Output on pad `abe_dmic_din3` (`0x4A100116`,
  GPIO 122) in mux mode 5. The stock OS uses 2 to 98 % duty, 66 % by default.
- Pad `dpm_emu17` (`0x4A1001D0`) is an LCD data line (`dispc2_data2`); never remux it.

## Keypad

OMAP4 keypad controller (`0x4A31C000`), 8 rows x 8 columns used, 23 keys. Matrix
positions and the colour layers are defined in `support/keymap.py`.

| Key | Row, column | Key | Row, column |
|---|---|---|---|
| 1 .. 9 | (0,0) (2,2) (1,2) (3,5) (3,4) (3,3) (2,5) (2,4) (1,0) | 0 | (1,4) |
| SPACE/BKSP | (1,5) | up, down | (1,3), (0,3) |
| big orange | (0,5) | big gray | (0,4) |
| blue | (3,6) | ESC | (3,7) |
| MENU/TAB | (2,7) | ALT/CTRL | (1,7) |
| SHIFT | (0,7) | ENTER | (0,6) |
| P1 | (2,6) | P2 | (1,6) |

## Touch

Semtech SX8650 resistive controller on I2C4 at 0x49. The pen-down interrupt is FPGA
interrupt source 0. Units without a touch panel just fail this probe.

## FPGA

Actel IGLOO+ on **GPMC chip select 1**, a 16 MiB window at `0x02000000`, 16-bit,
address/data multiplexed. Chip-select configuration (registers `CONFIG1..7`):
`0x01801110 0x00141801 0x33070715 0x0E079718 0x00171418 0x8D080000 0x00000F42`.

Register blocks (16-bit registers): **S** = `0x02020000`, **X** = `0x02080000`,
**Y** = `0x020C0000`. Most controls are pairs: writing a bit to the even offset sets it,
writing it to offset + 2 clears it.

### Interrupt controller

- Status bytes A, B, C, D at S+`0x08`, `0x0A`, `0x0C`, `0x0E`; write a bit to acknowledge.
- S+`0x10` / S+`0x12`: bit 7 = interrupt output enable, bit 6 = wake enable.
- Output to OMAP GPIO 9, rising edge.
- 24 sources, each with an enable bit in a set/clear pair:

| Src | Status bit | Enable (reg, mask) | Src | Status bit | Enable (reg, mask) |
|---|---|---|---|---|---|
| 0 | A.0 | S+0x14, 0x80 (touch pen) | 12 | C.1 | X+0x14, 0x80 |
| 1 | A.1 | S+0x14, 0x08 | 13 | C.2 | X+0x14, 0x10 |
| 2 | A.2 | S+0x18, 0x80 | 14 | C.4 | X+0x18, 0x10 |
| 3 | A.3 | S+0x48, 0x80 | 15 | D.0 | Y+0x14, 0x20 |
| 4 | A.4 | S+0x38, 0x08 | 16 | D.1 | Y+0x14, 0x80 |
| 5 | B.0 | S+0x4C, 0x80 | 17 | D.2 | Y+0x14, 0x04 |
| 6 | B.1 | S+0x38, 0x80 | 18 | D.7 | S+0x1C, 0x80 |
| 7 | B.2 | S+0x50, 0x80 | 19 | D.5 | S+0x20, 0x80 |
| 8 | B.3 | S+0x50, 0x10 | 20 | D.6 | S+0x2C, 0x80 |
| 9 | B.6 | S+0x3C, 0x80 | 21 | C.5 | X+0x14, 0x02 (FPGA I2C master) |
| 10 | B.7 | S+0x34, 0x80 | 22 | D.4 | S+0x58, 0x80 |
| 11 | C.0 | X+0x18, 0x80 (accessory serial wake) | 23 | D.3 | S+0x60, 0x80 |

The pen level can also be read directly at S+`0x14` bit 5.

### Outputs

| Output | Register (set / clear) | Bit |
|---|---|---|
| DECODE LED | S+0x28 / 0x2A | 1 |
| SCAN LED | S+0x28 / 0x2A | 2 |
| CTRL LED | S+0x18 / 0x1A | 2 |
| FUNC LED | S+0x18 / 0x1A | 0 |
| audio amplifier enable | S+0x98 / 0x9A | 3 |

The four LED outputs switch, but no LED on the unit itself lights; they probably serve the
accessory ports. The built-in speaker plays without the amplifier bit.

Beeper registers: S+`0x94` frequency divider (bits 5:0) and duration (bits 7:6),
S+`0x9C` amplitude, S+`0xA0` duration, start with S+`0x98` = 2, stop with S+`0x9A` = 2.
The beeper produces no sound under Linux yet.

## External Agent microcontroller

Atmel ATxmega16A4 on I2C4 at 0x42; interrupt on GPIO 10. Register/value protocol: write a
register number, then data. Register `0x01` returns the firmware version (3 bytes, for
example 2.56.0). It also handles battery status and temperature, charging limits, a wake
timer and warm boot; those registers are not mapped yet.

### LEDs

| LED | Registers | Values |
|---|---|---|
| right, green | pattern `0x12`, duration `0x13` | `0x0f` + `0xff` = on, `0xff` + `0xff` = blink, `0` + `0` = off |
| left, green (orange while charging) | pattern `0x14`, duration `0x15` | as above |
| middle, bi-colour | colour `0x16` (1 red, 2 green), pattern `0x17` (`0x01` solid, `0xf1` blink, `0` off), duration `0x18` | |
| LED1 | pattern `0x10`, duration `0x11` | no visible LED on this unit |

The high nibble of a pattern value selects the blink cadence; values other than those
above are not mapped.

## Audio

- TI TWL6040 on I2C1 at 0x4B, interrupt SPI 119, power enable GPIO 104, needs the
  TWL6030's CLK32KAUDIO. McPDM link (pads as left by the bootloader).
- **Built-in speaker**: the codec's AUXL/AUXR line outputs drive an external amplifier.
  Mixer: "Handsfree Left/Right Playback" = `HF DAC`, "AUXL/AUXR Playback Switch" on,
  volume "Handsfree Playback Volume" (0 to 29). The audio is McPDM channels 3 and 4. The
  codec's handsfree class-D outputs are not connected.
- The hardware accepts S32_LE at 88.2 or 96 kHz only; `/etc/asound.conf` converts.
- Headset outputs HSOL/HSOR and the headset microphone: not tested.
- Built-in microphone: digital microphone 1 (pads `abe_dmic_clk1`, `abe_dmic_din1`).
  Capture runs but delivers samples at the wrong rate.

## Wi-Fi and Bluetooth

TI WL1283 (WiLink 7): Wi-Fi 802.11a/b/g/n and Bluetooth in one chip.

- **Wi-Fi**: SDIO on MMC4, 4-bit; enable GPIO 100 (active high), interrupt GPIO 45; 26 MHz
  reference and TCXO clock. Firmware `wl128x-fw-5-sr.bin` / `-mr.bin` and `wl128x-nvs.bin`.
  The NVS file has no MAC address, so the driver derives one from the chip.
- **Bluetooth**: UART1, enable GPIO 103, host wake GPIO 139, 3 Mbaud after init. Needs
  `ti-connectivity/TIInit_10.6.15.bts`, the version from the unit's Windows CE image (TI
  patch `TI_P6_15.90` plus an audio-processor script); TI's public files of that name do
  not work on this unit. The ROM advertises Low Energy, but the controller refuses LE
  commands, so Linux uses it as BR/EDR only (kernel patch 0009).
- The two halves share the chip: resetting the Bluetooth core (for example by restarting
  its driver, or a failed init script) also crashes the Wi-Fi firmware.

## USB

- MUSB OTG controller with the TWL6030 PHY, used in device mode (USB serial gadget).
  High-speed enumeration fails on this port; it is limited to full speed.
- In download mode the bootloader enumerates as `05e0:200b`.
- An EHCI host controller exists; whether it is wired to the cradle is not known.

## Sensors

NXP/Freescale MMA8452Q accelerometer on I2C2 at 0x1C (`WHO_AM_I` = `0x2A`). Its interrupt
line is not known; values are read through IIO.

## Scanner and accessory ports

The two accessory ports carry a serial scan engine interface on UART2 (`0x4806C000`), 38400
baud in the stock OS, powered through the FPGA. Not supported under Linux yet.

## Not yet identified

- The chip at I2C1 0x6C (see "Power"), and whether the unit has a charger the OS manages.
- The consumer of VAUX3, and which rail feeds the LCD panel.
- The roles of the unknown bootloader GPIO outputs.
- The External Agent's battery and power registers.
- A broadcast clock event device: without one the deep idle states lose timers, so the
  kernel runs with `cpuidle.off=1`.
