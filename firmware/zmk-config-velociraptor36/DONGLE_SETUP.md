# Velociraptor36 Dongle Setup

The spare nice!nano v2 is the USB central. Both keyboard halves are BLE peripherals and require the
dongle to operate.

## Firmware artifacts

- `velociraptor36_dongle_studio.uf2`: spare nice!nano v2 dongle
- `velociraptor36_dongle_smoke.uf2`: diagnostic dongle without OLED or Studio
- `velociraptor36_usb_smoke.uf2`: diagnostic one-key USB-only firmware
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

If the OLED/Studio dongle keeps flashing the onboard blue LED and never appears over USB, flash
`velociraptor36_dongle_smoke.uf2` to the dongle after settings reset. If the smoke build works,
the remaining fault is in the OLED/Studio layer, not the split central role.

If the dongle smoke build also fails to appear as a USB device, flash
`velociraptor36_usb_smoke.uf2`. This image does not implement the keyboard; it only verifies
that the nice!nano can run a ZMK USB app instead of staying in bootloader.

The settings-reset image is only required when changing split roles, replacing a controller, or
recovering from broken split bonding. Normal firmware updates do not require it.

## RGB power behavior

- Status and profile RGB commands are temporarily disabled while the dongle setup is stabilized.
- The dongle no longer creates a virtual RGB strip; this keeps USB bring-up simpler.
- Basic underglow control can be restored after the dongle enumerates reliably.
- Both halves enter deep sleep after 15 minutes without activity.

## OLED

The dongle firmware includes first-pass support for the common 0.91 inch SSD1306 I2C OLED
at address `0x3C` and `128x32` resolution. Wire `SCK`/`SCL` to nice!nano `P0.20`,
`SDA` to `P0.17`, `GND` to `GND`, and `VCC` to `VCC`/`3V3`.

The dongle overlay is now a standalone dongle shield: mock kscan, physical layout, matrix
transform, and OLED only. It no longer includes the keyboard-half GPIO/encoder hardware.
