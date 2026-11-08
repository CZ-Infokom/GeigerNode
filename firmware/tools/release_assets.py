#!/usr/bin/env python3
"""Check flash HEX files and stage Flipper AVR Flasher release assets.

Flash AVR writes the fuse and lock bytes named in the .avr descriptor.
Those bytes stay the approved set from the 2026-10-01 fuse review.
"""

from __future__ import annotations

import argparse
import re
import shutil
import sys
from dataclasses import dataclass
from pathlib import Path

FLASH_BYTES = 32768
APPROVED_FUSES = {"LFUSE": "E2", "HFUSE": "D9", "EFUSE": "F5"}
LOCK = "FF"
SIGNATURE = "1E 95 16"
CHIP_NAME = "ATmega328PB"

# vMAJOR.MINOR.PATCH with an optional SemVer pre-release suffix.
# -beta, -rc, -pre, -alpha, and any other dot-separated identifier.
_VERSION_RE = re.compile(
    r"^v(?P<major>0|[1-9]\d*)\.(?P<minor>0|[1-9]\d*)\.(?P<patch>0|[1-9]\d*)"
    r"(?:-(?P<pre>[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$"
)
_FUSE_RE = {
    name: re.compile(rf"^{name}\s*:=\s*0x([0-9A-Fa-f]{{2}})\s*(?:#.*)?$")
    for name in APPROVED_FUSES
}


class ReleaseError(Exception):
    """A HEX file, version tag, or fuse byte failed a release check."""


@dataclass(frozen=True)
class Version:
    major: int
    minor: int
    patch: int
    prerelease: tuple[str, ...]

    @property
    def is_prerelease(self) -> bool:
        return bool(self.prerelease)


def parse_version(tag: str) -> Version:
    match = _VERSION_RE.fullmatch(tag)
    if match is None:
        raise ReleaseError(
            f"{tag!r} is not a version tag. Use vMAJOR.MINOR.PATCH "
            "or vMAJOR.MINOR.PATCH-identifier."
        )
    pre = match.group("pre")
    parts = tuple(pre.split(".")) if pre else ()
    return Version(
        int(match.group("major")),
        int(match.group("minor")),
        int(match.group("patch")),
        parts,
    )


def read_makefile_fuses(path: Path) -> dict[str, str]:
    found: dict[str, str] = {}
    for line_no, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        for name, pattern in _FUSE_RE.items():
            match = pattern.match(line)
            if match is None:
                continue
            if name in found:
                raise ReleaseError(f"{path}:{line_no}: {name} is set more than once")
            found[name] = match.group(1).upper()
    missing = [name for name in APPROVED_FUSES if name not in found]
    if missing:
        raise ReleaseError(f"{path} does not set {', '.join(missing)}")
    return found


def require_approved_fuses(path: Path) -> dict[str, str]:
    found = read_makefile_fuses(path)
    for name, approved in APPROVED_FUSES.items():
        if found[name] != approved:
            raise ReleaseError(
                f"{path} sets {name} to 0x{found[name]}. "
                f"Release descriptors keep 0x{approved}. Do not change that byte."
            )
    return found


def check_hex(path: Path) -> int:
    """Return the number of flash bytes. Reject anything except data and EOF."""
    text = path.read_bytes()
    if not text:
        raise ReleaseError(f"{path} is empty")
    if not text.endswith(b"\n"):
        raise ReleaseError(f"{path} must end with a newline")
    try:
        lines = text.decode("ascii").splitlines()
    except UnicodeDecodeError as exc:
        raise ReleaseError(f"{path} is not ASCII Intel HEX") from exc
    seen = bytearray(FLASH_BYTES)
    covered = 0
    saw_eof = False
    for line_no, line in enumerate(lines, 1):
        if saw_eof:
            raise ReleaseError(f"{path}:{line_no}: bytes follow the end record")
        try:
            record_type, address, data = _parse_record(line)
        except ReleaseError as exc:
            raise ReleaseError(f"{path}:{line_no}: {exc}") from exc
        if record_type == 0x01:
            if data:
                raise ReleaseError(f"{path}:{line_no}: end record carries data")
            saw_eof = True
            continue
        if record_type != 0x00:
            raise ReleaseError(
                f"{path}:{line_no}: record type {record_type:02X} is not flash data. "
                "HEX releases are data records and an end record only."
            )
        end = address + len(data)
        if end > FLASH_BYTES:
            raise ReleaseError(
                f"{path}:{line_no}: address {address:#06x} length {len(data)} "
                f"is past {FLASH_BYTES} flash bytes"
            )
        for offset, _value in enumerate(data):
            slot = address + offset
            if seen[slot]:
                raise ReleaseError(f"{path}:{line_no}: overlap at {slot:#06x}")
            seen[slot] = 1
        covered += len(data)
    if not saw_eof:
        raise ReleaseError(f"{path} has no end record")
    if covered == 0:
        raise ReleaseError(f"{path} has no flash bytes")
    return covered


def _parse_record(line: str) -> tuple[int, int, bytes]:
    if not line.startswith(":") or len(line) < 11 or (len(line) - 1) % 2 != 0:
        raise ReleaseError("malformed Intel HEX record")
    try:
        blob = bytes.fromhex(line[1:])
    except ValueError as exc:
        raise ReleaseError("record is not hexadecimal") from exc
    count = blob[0]
    if len(blob) != count + 5:
        raise ReleaseError("record length does not match its byte count")
    total = (sum(blob[:-1]) + blob[-1]) & 0xFF
    if total != 0:
        raise ReleaseError("checksum mismatch")
    address = (blob[1] << 8) | blob[2]
    return blob[3], address, blob[4:-1]


def asset_names(tag: str, protocol: str) -> tuple[str, str]:
    if protocol not in ("native", "radsens"):
        raise ReleaseError(f"unknown protocol {protocol!r}")
    return (
        f"GeigerNode-{tag}-{protocol}_flash.hex",
        f"GeigerNode-{tag}-{protocol}.avr",
    )


def avr_descriptor(hex_name: str) -> str:
    return (
        "Filetype: Flipper Dump AVR\n"
        "Version: 1\n"
        f"Chip name: {CHIP_NAME}\n"
        f"Signature: {SIGNATURE}\n"
        f"Lfuse: {APPROVED_FUSES['LFUSE']}\n"
        f"Hfuse: {APPROVED_FUSES['HFUSE']}\n"
        f"Efuse: {APPROVED_FUSES['EFUSE']}\n"
        f"Lock: {LOCK}\n"
        f"Dump_flash: {hex_name}\n"
    )


def release_notes(
    tag: str,
    commit: str,
    toolchain: str,
    tag_message: str,
) -> str:
    version = parse_version(tag)
    kind = "pre-release" if version.is_prerelease else "release"
    lines = [
        f"GeigerNode `{tag}` ({kind}).",
        "",
        "Flash images for the ATmega328PB. Copy one HEX file and its `.avr` "
        "descriptor to the Flipper SD card and select the descriptor name in AVR Flasher.",
        "",
        "| File | Image |",
        "| --- | --- |",
        (
            f"| `GeigerNode-{tag}-native_flash.hex` | Native protocol at I2C `0x67`. "
            "Flash data records and an end record. |"
        ),
        f"| `GeigerNode-{tag}-native.avr` | Flipper AVR Flasher descriptor for the native image. |",
        (
            f"| `GeigerNode-{tag}-radsens_flash.hex` | RadSens protocol at I2C `0x66`. "
            "Flash data records and an end record. |"
        ),
        f"| `GeigerNode-{tag}-radsens.avr` | Flipper AVR Flasher descriptor for the RadSens image. |",
        "",
        "The HEX files contain no EEPROM, fuse, or lock records. Each `.avr` descriptor "
        f"names the approved fuses LFUSE `{APPROVED_FUSES['LFUSE']}`, "
        f"HFUSE `{APPROVED_FUSES['HFUSE']}`, EFUSE `{APPROVED_FUSES['EFUSE']}`, "
        f"and lock `{LOCK}`. Flash AVR writes the flash image and those fuse and lock "
        "bytes, and chip-erases EEPROM. Do not change those bytes.",
        "",
        "After the programmer reset the LED is on for 500 ms, then 380 V starts. "
        "Stay clear of the HV section.",
        "",
        f"Commit `{commit}`.",
        f"Toolchain: {toolchain}.",
    ]
    message = tag_message.strip()
    if message:
        lines.extend(["", "## Tag message", "", message])
    lines.append("")
    return "\n".join(lines)


def package(
    tag: str,
    native_hex: Path,
    radsens_hex: Path,
    out_dir: Path,
    makefile: Path,
    commit: str,
    toolchain: str,
    tag_message: str,
) -> Version:
    version = parse_version(tag)
    require_approved_fuses(makefile)
    out_dir.mkdir(parents=True, exist_ok=True)
    for protocol, source in (("native", native_hex), ("radsens", radsens_hex)):
        check_hex(source)
        hex_name, avr_name = asset_names(tag, protocol)
        shutil.copyfile(source, out_dir / hex_name)
        (out_dir / avr_name).write_text(avr_descriptor(hex_name), encoding="utf-8")
    (out_dir / "RELEASE_NOTES.md").write_text(
        release_notes(tag, commit, toolchain, tag_message),
        encoding="utf-8",
    )
    (out_dir / "release.env").write_text(
        f"TAG={tag}\nPRERELEASE={'true' if version.is_prerelease else 'false'}\n",
        encoding="utf-8",
    )
    return version


def _cmd_classify(args: argparse.Namespace) -> int:
    version = parse_version(args.tag)
    print(f"tag={args.tag}")
    print(f"prerelease={'true' if version.is_prerelease else 'false'}")
    return 0


def _cmd_check_hex(args: argparse.Namespace) -> int:
    for path in args.hex:
        count = check_hex(path)
        print(f"{path}: {count} flash bytes")
    return 0


def _cmd_package(args: argparse.Namespace) -> int:
    message = ""
    if args.tag_message_file is not None:
        message = args.tag_message_file.read_text(encoding="utf-8")
    version = package(
        tag=args.tag,
        native_hex=args.native,
        radsens_hex=args.radsens,
        out_dir=args.out,
        makefile=args.makefile,
        commit=args.commit,
        toolchain=args.toolchain,
        tag_message=message,
    )
    kind = "pre-release" if version.is_prerelease else "release"
    print(f"packaged {args.tag} as a {kind} in {args.out}")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    classify = sub.add_parser("classify", help="print whether a tag is a pre-release")
    classify.add_argument("tag")
    classify.set_defaults(func=_cmd_classify)

    check = sub.add_parser("check-hex", help="require flash data records and an end record")
    check.add_argument("hex", nargs="+", type=Path)
    check.set_defaults(func=_cmd_check_hex)

    pack = sub.add_parser("package", help="write versioned HEX and Flipper .avr files")
    pack.add_argument("--tag", required=True)
    pack.add_argument("--native", required=True, type=Path)
    pack.add_argument("--radsens", required=True, type=Path)
    pack.add_argument("--out", required=True, type=Path)
    pack.add_argument("--makefile", required=True, type=Path)
    pack.add_argument("--commit", required=True)
    pack.add_argument("--toolchain", required=True)
    pack.add_argument("--tag-message-file", type=Path)
    pack.set_defaults(func=_cmd_package)

    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except ReleaseError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
