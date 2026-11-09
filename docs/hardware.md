# WT41N0 hardware reference

What is known about the Zebra / Symbol WT41N0 hardware and how Linux uses it. Register
offsets are relative to the block base given in each section. GPIO numbers are OMAP4
GPIO numbers (bank = number / 32 + 1, line = number % 32).

## Summary

| Part | Device | Linux driver | Status |
|---|---|---|---|
| SoC | TI OMAP4430 ES2.3, 2 x Cortex-A9, GP (unsigned) | - | working |
| RAM | 512 MiB LPDDR2 (package on package) | - | working |
| Storage | 4 GB eMMC (Toshiba, `004G60`) on MMC2, 8-bit | `omap_hsmmc` | working (read; Symbol partition layout, see "Storage") |
| Display | 240x320 RGB565 LCD, set up by the bootloader | `simple-framebuffer` + fbcon | working |
| Backlight | timer 9 PWM | `pwm-backlight` | working |
| Keypad backlight | TWL6030 PWM1 (as the stock `KeyLightDrv`) | `pwm-twl` + `pwm-leds` | working |
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
| Power button | TWL6030 RPWRON (not PWRON) | `twl6030-pwrbutton` | working (key events, wakes from suspend) |
| Ring scanner | Zebra SSI on UART2, trigger on GPIO 2 | `wt41n0-scan` (user space) | working |

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

### Power button, suspend and power-off

- The power button is on the TWL6030 **RPWRON** input, not PWRON: pressing it clears
  STS_HW_CONDITIONS (0x48 0x21) bit 1 and raises INT_STS_A (0x49 0xD0) bit 1 (RPWRON); it
  never raises PWRON (bit 0). The interrupt is masked until a driver unmasks it
  (`twl6030_interrupt_unmask`); `twl6030-pwrbutton` does.
- From power-off (TWL6030 DEVOFF, which `poweroff` does) the button does not start the
  unit: RPWRON is not a start event on this PMIC's configuration. The cradle (start
  condition 0x1F = 0x08) or a battery reinsertion (0x40) does.
- Suspend: `wt41n0-suspend`, used by the Power app, puts the unit in suspend-to-idle; the
  power button (and an RTC alarm) wakes it. The keypad would too, so its wakeup is
  switched off for the suspend. The display keeps its bootloader setup across it. The
  WL1283 loses its association, so its SDIO function is detached for the suspend and Wi-Fi
  is started again after.
- Mainline needed two fixes for a TWL6030 child to wake the system (patch 0013): the child
  irq_chip copies `dummy_irq_chip` with IRQCHIP_SKIP_SET_WAKE, so wake requests never
  armed the TWL6030 interrupt; and the PM notifier disabled that level-triggered line for
  the suspend, so any interrupt in between masked it until resume.
- Deep sleep (suspend to RAM) wakes again at once. Found so far: the backlight PWM keeps
  timer9 running and the dmtimer CPU PM notifier then reads it with its module off (an L3
  error whose interrupt wakes the CPU; patch 0011 stops it); and GPIO banks 2, 3, 4 and 6
  fail to idle ("l4-per-clkctrl: failed to disable"), more with Wi-Fi and Bluetooth up.
- The charger watchdog is fed by the battery driver, which cannot run while suspended:
  a long suspend in the cradle stops charging until the unit wakes.

### CPU speed and voltage

The bootloader leaves the MPU at 600 MHz (DPLL_MPU). Windows CE scaled it by load over
300 / 600 / 800 / 1000 MHz (registry `OMAPPMX\Policies\CPULOAD`, `MpuOppFrequency`) with
SmartReflex on.

Mainline Linux never sets VDD_MPU on OMAP4 (no `cpu0-supply`; the MPU voltage processor
stays disabled), so this BSP does what the PandaBoard's U-Boot does: `S01cpufreq` sets
VDD_MPU to 1.325 V once, and cpufreq-dt then changes only the clock, over 300 / 600 /
800 MHz. The kernel boots with the userspace governor, which holds 600 MHz until the
script has written the voltage. 1008 MHz (OPP-Nitro) also needs 1.375 V and the ABB in
forward body bias, and is left out. Checked: 10 minutes of two-core checksums at
800 MHz without an error, the CPU at 77 °C at most.

- Voltage write: OMAP voltage controller bypass (`PRM_VC_VAL_BYPASS`, 0x4A307BA0), PMIC
  slave 0x12 (TWL6030 SmartReflex I2C), register 0x55 (VCORE1 = VDD_MPU; 0x5B VCORE2 =
  IVA, 0x61 VCORE3 = core), then the VALID bit (24), which clears when it has been sent.
  The VCORE voltage registers read back 0 over I2C1.
- TWL6030 SMPS_OFFSET (0x48 register 0xE0) reads 0x7F: bit 3 is set, so the SMPS steps
  are 12.66 mV from 0.709 V (vsel 0x32 = 1.329 V). The last bypass write the bootloader
  left is core 0x2A = 1.228 V.
- Kernel bug to keep in mind: `omap_twl.c` reads SMPS_OFFSET before the TWL driver is up
  ("twl: not initialized"), falls back to "no offset" and so maps every voltage 0.1 V
  low. Nothing uses that mapping today (SmartReflex autocomp is off, the voltage
  processors are disabled); enabling either would set voltages 0.1 V higher than meant.

## Storage

eMMC on MMC2, `/dev/mmcblk1` under Linux: a 1888 MiB user area plus two 2 MiB boot
partitions. There is no MBR or GPT for the whole device. The Symbol monitor keeps its own
partition table at byte offset `0x300000` (3 MiB). Each entry is a 32-byte name, 4 bytes
not decoded, then start, length (both in 512-byte sectors) and flags (32-bit
little-endian). The entries that describe the user area:

| Region | Start | End | Content |
|---|---|---|---|
| BootLoader | 0 | 0.5 MiB | x-loader and bootloader (configuration header `CHSETTINGS` at 0) |
| Monitor | 1 MiB | 2 MiB | Symbol monitor (an image with the `ECEC` signature) |
| partition table | 3 MiB | | this table |
| Monitor data | 3.5 MiB | 4 MiB | written by the monitor (changes after USB downloads) |
| Config Block | 4 MiB | 4.5 MiB | written by the monitor |
| Splash screen | 4.5 MiB | 5.5 MiB | boot splash bitmap |
| *unassigned* | 5.5 MiB | 80 MiB | not in the table; all zeros |
| OS | 80 MiB | 200 MiB | the flashed OS image (Windows CE or this BSP's image) |
| Application | 200 MiB | 1368 MiB | Windows CE `\Application`: TexFAT volume with the unit's Windows CE configuration |
| Platform | 1368 MiB | 1880 MiB | Windows CE `\Platform`: TexFAT volume, empty |
| Scratchpad | 1880 MiB | 1888 MiB | written by the stock OS |

- Application and Platform each start with a one-sector MBR (one entry, type `0x0b`)
  followed by a TexFAT volume (exFAT with two FATs). Windows CE formats them itself when
  it cannot mount them, and again on a clean boot.
- The OS area can hold an old volume header after the end of the flashed image; it is a
  leftover, not a live volume.
- At every boot the monitor looks up the partition flagged `0x100000` (Application, its
  "persistent store") and reads that partition's first sector. If the table has no such
  entry, or the read fails, it does not boot ("Cannot find / Cannot read persistent
  store"). If the sector lacks the `0x55AA` signature, it boots anyway but passes a clean
  boot flag to the OS. It does not examine Platform or Scratchpad when booting.
- The monitor has a service console with commands that erase a partition, relocate a
  partition, repartition the device and write-protect blocks. None of them run on a
  normal boot.
- The user area runs in enhanced mode (`ENH_SIZE_MULT` = maximum): the 4 GB chip is
  configured as 1888 MiB of the more durable single-bit-per-cell storage. Permanent write
  protection is disabled for good, and power-on write protection is not active.
- Other table entries (Exec, Control, IST, External agent, SuperSpam) describe RAM or
  other chips: the monitor's 48 MiB staging area at `0x80268000`, and the firmware of the
  sensor, External Agent and FPGA.

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

| Output | Register (set / clear) | Bit | Linux GPIO line |
|---|---|---|---|
| accessory port power (ring scanner, UART2) | S+0x28 / 0x2A | 1 | `accessory-power` |
| accessory "scan" (named SCAN LED by the stock OS) | S+0x28 / 0x2A | 2 | `accessory-scan` |
| keypad CTRL LED output | S+0x18 / 0x1A | 2 | `keypad-ctrl-led` |
| keypad FUNC LED output | S+0x18 / 0x1A | 0 | `keypad-func-led` |
| audio amplifier enable | S+0x98 / 0x9A | 3 | `audio-amplifier` |
| accessory serial lines to UART2 | S+0x28 / 0x2A | 0 | `accessory-serial` |

The stock OS powers the scanner port with S+0x28 bit 1 (`Uart2\DevPower`, active high,
initially off); toggling it power-cycles an attached ring scanner. Bit 0 connects the
ports' serial lines to UART2: without it UART2's RX and CTS pins float. Only bits 0 to 2
of S+0x28 are writable; bit 4 reads as set. The keypad LED outputs
light nothing on the unit itself. The built-in speaker plays without the amplifier bit.

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
- A warm reboot does not reset the chip: Wi-Fi then comes up unable to transmit
  ("Tx stuck (in FW)"). The last shutdown step (`wt41n0-poweroff-radio`) drives both
  enable lines low before rebooting; a power-cycle resets it anyway.

## USB

- MUSB OTG controller with the TWL6030 PHY, used in device mode (USB serial gadget).
  High-speed enumeration fails on this port; it is limited to full speed.
- In download mode the bootloader enumerates as `05e0:200b`.
- An EHCI host controller exists; whether it is wired to the cradle is not known.

## Sensors

NXP/Freescale MMA8452Q accelerometer on I2C2 at 0x1C (`WHO_AM_I` = `0x2A`). Its interrupt
line is not known; values are read through IIO.

## Scanner and accessory ports

The two accessory ports (left and right) are equivalent and carry the ring scanner (tested
with an RS419, firmware `PAACBS00-002-R01`):

- Power: FPGA output `accessory-power`; serial lines: FPGA output `accessory-serial`.
  Both are needed.
- Serial: UART2 (`0x4806C000`, `/dev/ttyS1`), 38400 baud 8N1, no flow control, Zebra SSI
  protocol. **RTS must be deasserted**; the scanner does not answer while it is asserted
  (the stock OS opens the port with RTS control disabled).
- Trigger: wired to the terminal, OMAP GPIO 2 (bank 1, line 2), active low. The scanner
  does not scan on its own: the host sends `START_DECODE` on press and `STOP_DECODE` on
  release, so one trigger starts every attached scanner.
- Decode data arrives as raw text unless parameter `0xEE` (decode data packet format) is
  set to 1, after which each barcode comes as an SSI `DECODE_DATA` packet (barcode type,
  then the data) that the host acknowledges. The scanner as delivered also prefixes a
  code ID character (`A` for UPC-A); parameter `0x2D` = 0 turns that off.
- The trigger contact bounces; read the settled level after an edge.

`wt41n0-scan` implements this and prints one barcode per line. For other accessories,
`wt41n0-fpga serial on` switches on both outputs and leaves `/dev/ttyS1` to any program;
the connector's pinout and signal levels have not been measured yet.

## Not yet identified

- The chip at I2C1 0x6C (see "Power"), and whether the unit has a charger the OS manages.
- The consumer of VAUX3, and which rail feeds the LCD panel.
- The roles of the unknown bootloader GPIO outputs.
- The External Agent's battery and power registers.
- A broadcast clock event device: without one the deep idle states lose timers, so the
  kernel runs with `cpuidle.off=1`.
