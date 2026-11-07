#!/usr/bin/env python3
"""Canned-byte tests for the I2C viewer decoders. No serial."""

from __future__ import annotations

import unittest

from geigernode_viewer import (
    Bme680Calib,
    Bme680Reading,
    SampleHistory,
    Snapshot,
    calc_gas_resistance,
    calc_temperature,
    decode_native,
    decode_radsens,
    encode_native_calibration,
    encode_native_histogram_clear,
    encode_native_holdoff,
    encode_native_window,
    encode_radsens_address,
    encode_radsens_byte,
    encode_radsens_sensitivity,
    format_elapsed,
    format_elapsed_precise,
    nearest_point,
    read_spans,
    record_snapshot,
    render_csv,
    time_from_x,
    x_from_time,
)


class NativeDecodeTest(unittest.TestCase):
    def test_native_bank_layout(self) -> None:
        # Matches the 2026-11-06 ESP32-C3 pass shape:
        # ID 47 4E, protocol 01, caps 03, dose 100 nSv/h, CPM×10 108,
        # status VALID|HV_FOLLOWING + 1 attempt, cal 0x00093B14.
        bank = bytearray(0x1E)
        bank[0:2] = bytes.fromhex("474E")
        bank[2] = 0x01
        bank[3] = 0x03
        bank[4:8] = (100).to_bytes(4, "big")
        bank[8:12] = (108).to_bytes(4, "big")
        # Wire order is big-endian: 0x0C = attempts, 0x0D = flags.
        # 01 05 is the 2026-11-06 bench reading: 1 attempt, VALID | HV_FOLLOWING.
        bank[0x0C] = 0x01
        bank[0x0D] = 0x01 | 0x04
        bank[0x0E:0x12] = (42).to_bytes(4, "big")
        bank[0x12:0x16] = (604948).to_bytes(4, "big")
        bank[0x16:0x1A] = (90).to_bytes(4, "big")
        bank[0x1A:0x1E] = (500000).to_bytes(4, "big")

        n = decode_native(bytes(bank))
        self.assertEqual(n.device_id, 0x474E)
        self.assertEqual(n.protocol, 1)
        self.assertEqual(n.capabilities, 0x03)
        self.assertEqual(n.dose_nsvh, 100)
        self.assertAlmostEqual(n.dose_usvh, 0.1)
        self.assertEqual(n.cpm_x10, 108)
        self.assertAlmostEqual(n.cpm, 10.8)
        self.assertIn("VALID", n.status_names())
        self.assertIn("HV_FOLLOWING", n.status_names())
        self.assertEqual(n.hv_attempts, 1)
        self.assertEqual(n.sequence, 42)
        self.assertEqual(n.cal_q16, 604948)
        self.assertAlmostEqual(n.cal_nsvh_per_cpm, 604948 / 65536.0)
        self.assertEqual(n.count, 90)
        self.assertEqual(n.elapsed_ms, 500000)
        self.assertEqual(n.window_s, 0)
        self.assertEqual(n.holdoff_us, 0)

    def test_native_v2_window_and_holdoff(self) -> None:
        bank = bytearray(0x22)
        bank[0:2] = bytes.fromhex("474E")
        bank[2] = 0x02
        bank[3] = 0x07
        bank[0x1E:0x20] = (300).to_bytes(2, "big")
        bank[0x20:0x22] = (300).to_bytes(2, "big")
        n = decode_native(bytes(bank))
        self.assertEqual(n.protocol, 2)
        self.assertEqual(n.window_s, 300)
        self.assertEqual(n.holdoff_us, 300)
        self.assertFalse(n.has_histogram)

    def test_native_v2_histogram(self) -> None:
        bank = bytearray(0x39)
        bank[0:2] = bytes.fromhex("474E")
        bank[2] = 0x02
        bank[3] = 0x0F
        bank[0x1E:0x20] = (300).to_bytes(2, "big")
        bank[0x20:0x22] = (300).to_bytes(2, "big")
        bank[0x22:0x26] = (151).to_bytes(4, "big")
        bins = (0, 0, 6, 0, 0, 0, 0, 0, 78)
        for index, count in enumerate(bins):
            offset = 0x26 + index * 2
            bank[offset : offset + 2] = count.to_bytes(2, "big")
        bank[0x38] = 0x00
        n = decode_native(bytes(bank))
        self.assertTrue(n.has_histogram)
        self.assertEqual(n.min_interval_us, 151)
        self.assertEqual(n.histogram_bins, bins)
        self.assertFalse(n.histogram_saturated)

        bank[0x36:0x38] = (65535).to_bytes(2, "big")
        bank[0x38] = 0x01
        n = decode_native(bytes(bank))
        self.assertTrue(n.histogram_saturated)
        self.assertEqual(n.histogram_bins[8], 65535)

    def test_status_00_07_is_full_valid_window(self) -> None:
        bank = bytearray(0x1E)
        bank[0:2] = bytes.fromhex("474E")
        bank[0x0C:0x0E] = bytes.fromhex("0007")
        n = decode_native(bytes(bank))
        self.assertEqual(n.hv_attempts, 0)
        self.assertEqual(n.status_names(), ["VALID", "WINDOW_FULL", "HV_FOLLOWING"])


class RadSensDecodeTest(unittest.TestCase):
    def test_recorded_static_and_sensitivity(self) -> None:
        # Recorded bench case: static 00 00 3C → 6.0 µR/h; sensitivity 41 00 → 65.
        id_fw = bytes.fromhex("7D04")
        radiation = bytes.fromhex("00003C") + bytes.fromhex("00003C")
        hv = 0x00
        controls = bytes([0x66, 0x01, 0x41, 0x00, 0x01])

        r = decode_radsens(id_fw, radiation, hv, controls)
        self.assertEqual(r.device_id, 0x7D)
        self.assertEqual(r.firmware, 0x04)
        self.assertEqual(r.static_x10, 60)
        self.assertAlmostEqual(r.static_urh, 6.0)
        self.assertAlmostEqual(r.static_usvh, 0.060)
        self.assertEqual(r.sensitivity, 65)
        self.assertEqual(r.i2c_address, 0x66)
        self.assertEqual(r.hv_generator, 1)
        self.assertEqual(r.led_enable, 1)
        self.assertEqual(r.hv_status_name(), "following")


class Bme680CompensateTest(unittest.TestCase):
    def test_temperature_fixed_vector(self) -> None:
        # Hand-checked integer path from the Bosch formula.
        cal = Bme680Calib(par_t1=1000, par_t2=1000, par_t3=0)
        temp_x100 = calc_temperature(500000, cal)
        # var1 = 62500 - 2000 = 60500
        # var2 = (60500 * 1000) >> 11 = 29541
        # temp = ((29541 * 5) + 128) >> 8 = 577
        self.assertEqual(cal.t_fine, 29541)
        self.assertEqual(temp_x100, 577)

    def test_gas_range0_published_constants(self) -> None:
        cal = Bme680Calib(range_sw_err=0)
        # gas_adc == 512 → var2 == var1, gas_res == (var3 + var2/2) / var2
        gas = calc_gas_resistance(512, 0, cal)
        var1 = (1340 * 2147483647) >> 16
        var2 = ((512 << 15) - 16777216) + var1
        var3 = (4096000000 * var1) >> 9
        expected = (var3 + (var2 >> 1)) // var2
        self.assertEqual(gas, expected)
        self.assertGreater(gas, 0)


class HistoryTest(unittest.TestCase):
    def test_ring_drops_oldest_and_keeps_elapsed(self) -> None:
        history = SampleHistory(maxlen=3)
        for index, value in enumerate((1.0, 2.0, 3.0, 4.0)):
            history.add("cpm", value, when=1000.0 + index)
        self.assertEqual(history.points("cpm"), [(1.0, 2.0), (2.0, 3.0), (3.0, 4.0)])
        self.assertEqual(history.sample_count(), 3)
        history.clear()
        self.assertEqual(history.sample_count(), 0)
        self.assertIsNone(history.t0)

    def test_record_native_and_bme(self) -> None:
        bank = bytearray(0x1E)
        bank[0:2] = bytes.fromhex("474E")
        bank[2] = 1
        bank[4:8] = (100).to_bytes(4, "big")
        bank[8:12] = (108).to_bytes(4, "big")
        native = decode_native(bytes(bank))
        bme = Bme680Reading(22.0, 982.18, 41.6, 374765.0, True, False, True)
        history = SampleHistory()
        record_snapshot(
            history,
            Snapshot(port="test", mode="native", native=native, bme=bme),
            when=50.0,
        )
        self.assertAlmostEqual(history.points("dose_usvh")[0][1], 0.1)
        self.assertAlmostEqual(history.points("cpm")[0][1], 10.8)
        self.assertAlmostEqual(history.points("temperature_c")[0][1], 22.0)
        self.assertAlmostEqual(history.points("gas_kohm")[0][1], 374.765)

        stale = Bme680Reading(1.0, 1.0, 1.0, 1.0, False, False, False)
        record_snapshot(
            history,
            Snapshot(port="test", mode="native", bme=stale),
            when=51.0,
        )
        self.assertEqual(len(history.points("temperature_c")), 1)
        self.assertEqual(history.points("gas_kohm"), [(0.0, 374.765)])

    def test_radsens_derived_cpm(self) -> None:
        reading = decode_radsens(
            bytes.fromhex("7D04"),
            bytes.fromhex("00003C00003C"),
            0x00,
            bytes([0x66, 0x01, 0x41, 0x00, 0x01]),
        )
        history = SampleHistory()
        record_snapshot(
            history,
            Snapshot(port="test", mode="radsens", radsens=reading),
            when=10.0,
        )
        self.assertAlmostEqual(history.points("static_usvh")[0][1], 0.060)
        self.assertAlmostEqual(history.points("static_cpm")[0][1], 6.5)
        self.assertEqual(format_elapsed(45), "45s")
        self.assertEqual(format_elapsed(120), "2m")
        self.assertEqual(format_elapsed(7200), "2.0h")


class RegisterWriteTest(unittest.TestCase):
    def test_native_map_splits_at_bridge_limit(self) -> None:
        spans = read_spans(0x39)
        self.assertEqual(spans, [(0, 32), (32, 25)])
        self.assertEqual(sum(length for _, length in spans), 0x39)

    def test_native_and_radsens_payloads(self) -> None:
        self.assertEqual(encode_native_window(300), bytes.fromhex("1E012C"))
        self.assertEqual(encode_native_holdoff(300), bytes.fromhex("20012C"))
        self.assertEqual(encode_native_histogram_clear(), bytes.fromhex("3801"))
        self.assertEqual(encode_native_calibration(604948), bytes.fromhex("1200093B14"))
        self.assertEqual(encode_radsens_sensitivity(65), bytes.fromhex("124100"))
        self.assertEqual(encode_radsens_address(0x66), bytes.fromhex("1066"))
        self.assertEqual(encode_radsens_byte(0x11, 0), bytes.fromhex("1100"))
        with self.assertRaises(ValueError):
            encode_native_calibration(0)
        with self.assertRaises(ValueError):
            encode_native_window(0)
        with self.assertRaises(ValueError):
            encode_native_holdoff(301)
        with self.assertRaises(ValueError):
            encode_radsens_address(0x02)
        with self.assertRaises(ValueError):
            encode_radsens_sensitivity(511)

    def test_csv_joins_times_and_appends_histogram(self) -> None:
        history = SampleHistory()
        history.add("dose_usvh", 0.1, when=10.0)
        history.add("cpm", 10.8, when=10.0)
        history.add("cpm", 11.0, when=11.0)
        bank = bytearray(0x39)
        bank[0:2] = bytes.fromhex("474E")
        bank[2] = 2
        bank[3] = 0x0F
        bank[0x22:0x26] = (151).to_bytes(4, "big")
        bank[0x26:0x28] = (6).to_bytes(2, "big")
        text = render_csv(history, decode_native(bytes(bank)))
        self.assertIn("elapsed_s,dose_usvh,cpm", text)
        self.assertIn("0.000,0.1,10.8", text)
        self.assertIn("1.000,,11", text)
        self.assertIn("histogram_bin,count", text)
        self.assertIn("<50 µs,6", text)
        self.assertIn("min_gap_us,151", text)


class CursorTest(unittest.TestCase):
    def test_nearest_sample_and_axis_mapping(self) -> None:
        points = [(0.0, 1.0), (10.0, 2.0), (20.0, 4.0)]
        self.assertEqual(nearest_point(points, 10.4), (10.0, 2.0))
        self.assertEqual(nearest_point(points, 15.0), (10.0, 2.0))
        self.assertEqual(nearest_point(points, 15.1), (20.0, 4.0))
        self.assertEqual(nearest_point(points, -5.0), (0.0, 1.0))
        px = x_from_time(10.0, 56, 200, 0.0, 20.0)
        self.assertAlmostEqual(time_from_x(px, 56, 200, 0.0, 20.0), 10.0)
        self.assertEqual(format_elapsed_precise(754), "12m 34s")


if __name__ == "__main__":
    unittest.main()
