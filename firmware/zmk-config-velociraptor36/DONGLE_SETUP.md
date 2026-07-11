# Velociraptor36 Dongle Setup

The spare nice!nano v2 is the USB central. Both keyboard halves are BLE peripherals and require the
dongle to operate.

## Firmware artifacts

- `velociraptor36_dongle_studio.uf2`: spare nice!nano v2 dongle
- `velociraptor36_left_peripheral.uf2`: left keyboard half
- `velociraptor36_right_peripheral.uf2`: right keyboard half
- `settings_reset_nice_nano_v2.uf2`: temporary reset image used on all three controllers

## First-time pairing

1. Turn off both keyboard halves.
2. Flash `settings_reset_nice_nano_v2.uf2` to the dongle, left half, and right half.
3. Flash `velociraptor36_dongle_studio.uf2` to the dongle and keep it connected over USB.
4. Flash `velociraptor36_left_peripheral.uf2` to the left half, then power it on near the dongle.
5. Flash `velociraptor36_right_peripheral.uf2` to the right half, then power it on near the dongle.
6. Allow several seconds for both peripherals to bond with the dongle.

The settings-reset image is only required when changing split roles, replacing a controller, or
recovering from broken split bonding. Normal firmware updates do not require it.

## RGB power behavior

- Status and profile RGB commands are temporarily disabled on the dongle while OLED/split
  central bring-up is being debugged.
- Brightness is capped at 30 percent.
- After 30 seconds without key or encoder activity, both halves temporarily turn RGB off.
- Activity restores RGB only when it was enabled before entering idle.
- A manual RGB off therefore remains off after idle/wake.
- Both halves enter deep sleep after 15 minutes without activity.

## OLED

The dongle firmware has first-pass support for the common 0.91 inch SSD1306 I2C OLED
at address `0x3C` and `128x32` resolution. Wire `SCK`/`SCL` to nice!nano `P0.20`,
`SDA` to `P0.17`, `GND` to `GND`, and `VCC` to `VCC`/`3V3`.

The current OLED setup uses ZMK's built-in status screen. A custom two-battery widget can
be added once the first display build confirms the module address and resolution.
