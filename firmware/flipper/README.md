# Flipper AVR Flasher images

These are the ATmega328PB flash images for the Flipper AVR Flasher. The current pair is `v0.2.1-beta.1`. Copy one file onto the Flipper SD card and program it from the AVR Flasher app, with no computer attached to the target.

| File | Image | Flash bytes |
| --- | --- | --- |
| `GeigerNode-v0.2.1-beta.1-native_flash.hex` | Native protocol v2 at `0x67`. Window default 300 s, ceiling 400 s. Holdoff default 300 µs | 10632 |
| `GeigerNode-v0.2.1-beta.1-radsens_flash.hex` | RadSens at `0x66`, firmware byte `0x04`. Window fixed at 300 s. Holdoff fixed at 300 µs | 9178 |

The HEX files are data records and an end record only. They contain no EEPROM, fuse, or lock bytes. Each `.avr` descriptor names the approved fuses LFUSE `E2`, HFUSE `D9`, EFUSE `F5`, and lock `FF`. Do not change those bytes. On 2026-11-07 the beta.1 pair was copied to Chir3n at `/ext/apps_data/avr_isp/`. Select `GeigerNode-v0.2.1-beta.1-native` or `GeigerNode-v0.2.1-beta.1-radsens` in Flash AVR. Earlier images on the card are unchanged.

Flash AVR chip-erases the part, so EEPROM is wiped. Native v1 has no stored address. The RadSens image comes back as address `0x66` and sensitivity 65. The programmer reset holds the LED on for 500 ms, then 380 V starts. Stay clear of the HV section.

Rebuild from `firmware/`:

```sh
make PROTOCOL=native
make PROTOCOL=radsens
cp bin/native/geigernode.hex flipper/GeigerNode-v0.2.1-beta.1-native_flash.hex
cp bin/radsens/geigernode.hex flipper/GeigerNode-v0.2.1-beta.1-radsens_flash.hex
```
