# Flipper AVR Flasher images

These are the ATmega328PB flash images for the Flipper AVR Flasher. The pair in this directory is `v0.2.1-beta.2`, tagged on `1872896`. It is the native histogram map, the histogram clear from `0x38` or an accepted holdoff write, and the static-RAM cuts: no TWI `txn[]` copy, the fault-LED tables encoded in `led_fault()`, and the RadSens image without `cps_extra[]`. Copy one file onto the Flipper SD card and program it from the AVR Flasher app, with no computer attached to the target. `v0.2.1-beta.4` is published from the version tag, not stored here.

| File | Image | Flash bytes |
| --- | --- | --- |
| `GeigerNode-v0.2.1-beta.2-native_flash.hex` | Native protocol v2 at `0x67`, histogram at `0x22`..`0x38`. Window default 300 s, ceiling 400 s. Holdoff default 300 µs. Text 11240, data 12, bss 1492 | 11240 |
| `GeigerNode-v0.2.1-beta.2-radsens_flash.hex` | RadSens at `0x66`, firmware byte `0x04`. Window fixed at 300 s. Holdoff fixed at 300 µs. Text 8866, data 2, bss 809 | 8866 |

The HEX files are data records and an end record only. They contain no EEPROM, fuse, or lock bytes. Each `.avr` descriptor names the approved fuses LFUSE `E2`, HFUSE `D9`, EFUSE `F5`, and lock `FF`. Do not change those bytes. On 2026-11-08 the tagged beta.2 pair was copied to Chir3n at `/ext/apps_data/avr_isp/`, replacing the earlier same-name files. Select `GeigerNode-v0.2.1-beta.2-native` or `GeigerNode-v0.2.1-beta.2-radsens` in Flash AVR. Earlier images on the card, including beta.1, are unchanged.

Flash AVR chip-erases the part, so EEPROM is wiped. Native v2 has no stored address. The RadSens image comes back as address `0x66` and sensitivity 65. The programmer reset holds the LED on for 500 ms, then 380 V starts. Stay clear of the HV section.

Rebuild from `firmware/`:

```sh
make PROTOCOL=native
make PROTOCOL=radsens
cp bin/native/geigernode.hex flipper/GeigerNode-v0.2.1-beta.2-native_flash.hex
cp bin/radsens/geigernode.hex flipper/GeigerNode-v0.2.1-beta.2-radsens_flash.hex
```
