#!/usr/bin/env python3
"""Checks for version tags, flash HEX files, and Flipper descriptors."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import release_assets
from release_assets import ReleaseError


ROOT = Path(__file__).resolve().parents[2]
TINY_HEX = ":020000000102FB\n:00000001FF\n"


class VersionTest(unittest.TestCase):
    def test_plain_version_is_a_release(self) -> None:
        version = release_assets.parse_version("v0.2.0")
        self.assertEqual((version.major, version.minor, version.patch), (0, 2, 0))
        self.assertFalse(version.is_prerelease)

    def test_known_suffixes_are_prereleases(self) -> None:
        for tag in (
            "v0.2.0-pre.1",
            "v0.2.1-beta.2",
            "v1.0.0-rc.1",
            "v1.0.0-rc",
            "v1.0.0-alpha",
            "v1.0.0-alpha.2",
            "v2.0.0-preview.1",
            "v3.1.4-dev",
        ):
            with self.subTest(tag=tag):
                self.assertTrue(release_assets.parse_version(tag).is_prerelease)

    def test_rejects_tags_that_are_not_versions(self) -> None:
        for tag in ("v1.2", "v01.2.3", "v1.2.3-", "v1.2.3-beta.", "0.2.0", "v1.2.3+build"):
            with self.subTest(tag=tag):
                with self.assertRaises(ReleaseError):
                    release_assets.parse_version(tag)


class HexTest(unittest.TestCase):
    def test_tiny_image_passes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "image.hex"
            path.write_text(TINY_HEX, encoding="ascii")
            self.assertEqual(release_assets.check_hex(path), 2)
            path.write_bytes(TINY_HEX.replace("\n", "\r\n").encode("ascii"))
            self.assertEqual(release_assets.check_hex(path), 2)

    def test_rejects_extended_address_and_a_short_file(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "bad.hex"
            path.write_text(":020000040000FA\n:00000001FF\n", encoding="ascii")
            with self.assertRaises(ReleaseError):
                release_assets.check_hex(path)
            path.write_text(":017FFF000081\n:00000001FF\n", encoding="ascii")
            self.assertEqual(release_assets.check_hex(path), 1)
            path.write_text(":027FFF00000080\n:00000001FF\n", encoding="ascii")
            with self.assertRaises(ReleaseError):
                release_assets.check_hex(path)

    def test_rejects_overlap_and_bad_checksum(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "bad.hex"
            path.write_text(":0100000000FF\n:0100000001FE\n:00000001FF\n", encoding="ascii")
            with self.assertRaises(ReleaseError):
                release_assets.check_hex(path)
            path.write_text(":0100000000FE\n:00000001FF\n", encoding="ascii")
            with self.assertRaises(ReleaseError):
                release_assets.check_hex(path)

    def test_committed_flipper_hex_is_flash_only(self) -> None:
        paths = sorted((ROOT / "firmware" / "flipper").glob("*.hex"))
        self.assertGreaterEqual(len(paths), 2)
        for path in paths:
            with self.subTest(path=path.name):
                self.assertGreater(release_assets.check_hex(path), 0)


class PackageTest(unittest.TestCase):
    def test_makefile_keeps_the_approved_fuses(self) -> None:
        found = release_assets.require_approved_fuses(ROOT / "firmware" / "Makefile")
        self.assertEqual(found, {"LFUSE": "E2", "HFUSE": "D9", "EFUSE": "F5"})

    def test_a_different_fuse_byte_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "Makefile"
            path.write_text("LFUSE := 0xE2\nHFUSE := 0xD9\nEFUSE := 0x00\n", encoding="utf-8")
            with self.assertRaises(ReleaseError):
                release_assets.require_approved_fuses(path)

    def test_package_writes_the_flipper_pair(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            native = root / "native.hex"
            radsens = root / "radsens.hex"
            native.write_text(TINY_HEX, encoding="ascii")
            radsens.write_text(":01000000AA55\n:00000001FF\n", encoding="ascii")
            out = root / "dist"
            version = release_assets.package(
                tag="v0.2.1-beta.2",
                native_hex=native,
                radsens_hex=radsens,
                out_dir=out,
                makefile=ROOT / "firmware" / "Makefile",
                commit="abc",
                toolchain="avr-gcc (AVR_8_bit_GNU_Toolchain_4.0.0_52) 15.1.0",
                tag_message="histogram clear\n",
            )
            self.assertTrue(version.is_prerelease)
            descriptor = (out / "GeigerNode-v0.2.1-beta.2-native.avr").read_text(encoding="utf-8")
            self.assertEqual(
                descriptor,
                "Filetype: Flipper Dump AVR\n"
                "Version: 1\n"
                "Chip name: ATmega328PB\n"
                "Signature: 1E 95 16\n"
                "Lfuse: E2\n"
                "Hfuse: D9\n"
                "Efuse: F5\n"
                "Lock: FF\n"
                "Dump_flash: GeigerNode-v0.2.1-beta.2-native_flash.hex\n",
            )
            self.assertEqual(
                (out / "GeigerNode-v0.2.1-beta.2-native_flash.hex").read_bytes(),
                native.read_bytes(),
            )
            notes = (out / "RELEASE_NOTES.md").read_text(encoding="utf-8")
            self.assertIn("pre-release", notes)
            self.assertIn("histogram clear", notes)
            self.assertIn("LFUSE `E2`", notes)
            env = (out / "release.env").read_text(encoding="utf-8")
            self.assertEqual(env, "TAG=v0.2.1-beta.2\nPRERELEASE=true\n")

    def test_a_stable_tag_is_not_a_prerelease(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            image = root / "image.hex"
            image.write_text(TINY_HEX, encoding="ascii")
            out = root / "dist"
            version = release_assets.package(
                tag="v0.2.0",
                native_hex=image,
                radsens_hex=image,
                out_dir=out,
                makefile=ROOT / "firmware" / "Makefile",
                commit="abc",
                toolchain="avr-gcc",
                tag_message="",
            )
            self.assertFalse(version.is_prerelease)
            self.assertIn(
                "PRERELEASE=false",
                (out / "release.env").read_text(encoding="utf-8"),
            )
            self.assertIn("(release)", (out / "RELEASE_NOTES.md").read_text(encoding="utf-8"))

    def test_committed_descriptors_name_the_approved_fuses(self) -> None:
        paths = sorted((ROOT / "firmware" / "flipper").glob("*.avr"))
        self.assertGreaterEqual(len(paths), 2)
        for path in paths:
            text = path.read_text(encoding="utf-8")
            with self.subTest(path=path.name):
                self.assertIn("Lfuse: E2\n", text)
                self.assertIn("Hfuse: D9\n", text)
                self.assertIn("Efuse: F5\n", text)
                self.assertIn("Lock: FF\n", text)
                self.assertIn("Signature: 1E 95 16\n", text)


if __name__ == "__main__":
    unittest.main()
