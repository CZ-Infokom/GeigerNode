# Flipper AVR Flasher images

These are the v0.2.0 flash images for the ATmega328PB. Copy one file onto the Flipper SD card and program it from the AVR Flasher app, with no computer attached to the target.

| File | Image | Flash bytes |
| --- | --- | --- |
| `geigernode-native.hex` | Native I2C protocol v1 at `0x67` | 8982 |
| `geigernode-radsens.hex` | ClimateGuard RadSens slave at `0x66`, firmware byte `0x04` | 9048 |

Both files are Intel HEX, data records and an end record only. They contain no EEPROM, fuse, or lock bytes. AVR Flasher's Flash AVR action writes the selected file. A full-chip dump from that app can also carry fuses; these two files do not. Do not write fuses. The approved bytes remain LFUSE `0xE2`, HFUSE `0xD9`, EFUSE `0xF5`.

Flash AVR chip-erases the part, so EEPROM is wiped. Native v1 has no stored address. The RadSens image comes back as address `0x66` and sensitivity 65. The programmer reset holds the LED on for 500 ms, then 380 V starts. Stay clear of the HV section.

Rebuild from `firmware/`:

```sh
make PROTOCOL=native
make PROTOCOL=radsens
cp bin/native/geigernode.hex flipper/geigernode-native.hex
cp bin/radsens/geigernode.hex flipper/geigernode-radsens.hex
```
