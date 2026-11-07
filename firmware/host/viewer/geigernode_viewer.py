#!/usr/bin/env python3
"""GeigerNode I2C measurement viewer over the ESP32-C3 USB bridge.

Reads native protocol v2 at 0x67 or the RadSens map at 0x66, plus the BME680
at 0x76. RadSens 0x09/0x0A are never read. Register writes stay disabled until
the warning is accepted, and each write confirms again. A RadSens HV write
starts or stops 380 V and does not change the setpoint. BME680 measurement
registers are written for the sensor cycle. Fuses and the programmer port
are not touched.
"""

from __future__ import annotations

import argparse
import csv
import glob
import io
import queue
import re
import struct
import sys
import time
from collections import deque
from dataclasses import dataclass, field
from typing import Optional

import serial
from PyQt6.QtCore import Qt, QPointF, QThread, pyqtSignal
from PyQt6.QtGui import QColor, QPainter, QPalette, QPen
from PyQt6.QtWidgets import (
    QApplication,
    QCheckBox,
    QComboBox,
    QFileDialog,
    QFormLayout,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QScrollArea,
    QSizePolicy,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

DEFAULT_PORT_GLOB = (
    "/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_*-if00"
)
BAUD = 115200
POLL_INTERVAL_S = 1.0
BRIDGE_TIMEOUT_S = 1.5
BRIDGE_MAX_TRANSFER = 32  # USB-I2C bridge rejects a longer read or write
NATIVE_MAP_SIZE = 0x39
HISTORY_MAX = 7200  # about two hours at one sample per second; RAM only

NATIVE_ADDR = 0x67
RADSENS_ADDR = 0x66
BME_ADDR = 0x76
NATIVE_ID = 0x474E
RADSENS_ID = 0x7D
BME_CHIP_ID = 0x61


# ---------------------------------------------------------------------------
# Byte helpers
# ---------------------------------------------------------------------------

def be16(data: bytes, off: int = 0) -> int:
    return struct.unpack_from(">H", data, off)[0]


def be24(data: bytes, off: int = 0) -> int:
    return (data[off] << 16) | (data[off + 1] << 8) | data[off + 2]


def be32(data: bytes, off: int = 0) -> int:
    return struct.unpack_from(">I", data, off)[0]


def read_spans(total: int, chunk: int = BRIDGE_MAX_TRANSFER) -> list[tuple[int, int]]:
    """(pointer, length) pairs that cover `total` bytes without exceeding `chunk`."""
    if total < 1 or chunk < 1:
        raise ValueError(f"bad span {total} / {chunk}")
    spans: list[tuple[int, int]] = []
    offset = 0
    while offset < total:
        length = min(chunk, total - offset)
        spans.append((offset, length))
        offset += length
    return spans


def le16(data: bytes, off: int = 0) -> int:
    return struct.unpack_from("<H", data, off)[0]


def u8_signed(v: int) -> int:
    return v - 256 if v >= 128 else v


# ---------------------------------------------------------------------------
# Native / RadSens decode
# ---------------------------------------------------------------------------

NATIVE_STATUS_BITS = (
    (0x01, "VALID"),
    (0x02, "WINDOW_FULL"),
    (0x04, "HV_FOLLOWING"),
    (0x08, "HV_SETTLING"),
    (0x10, "HV_COOLDOWN"),
    (0x20, "HV_FAULT"),
    (0x40, "OVERFLOW"),
)

RADSENS_HV_NAMES = {
    0: "following",
    1: "cooldown",
    2: "given up",
}


NATIVE_HISTOGRAM_BIN_LABELS = (
    "<50 µs",
    "50–100 µs",
    "100–200 µs",
    "200–500 µs",
    "500 µs–1 ms",
    "1–2 ms",
    "2–5 ms",
    "5–10 ms",
    "≥10 ms",
)

NATIVE_CAPS_HISTOGRAM = 0x08
NATIVE_HISTOGRAM_SATURATED = 0x01


@dataclass
class NativeReading:
    device_id: int
    protocol: int
    capabilities: int
    dose_nsvh: int
    cpm_x10: int
    status_lo: int
    hv_attempts: int
    sequence: int
    cal_q16: int
    count: int
    elapsed_ms: int
    window_s: int = 0
    holdoff_us: int = 0
    min_interval_us: Optional[int] = None
    histogram_bins: tuple[int, ...] = ()
    histogram_flags: int = 0

    @property
    def dose_usvh(self) -> float:
        return self.dose_nsvh / 1000.0

    @property
    def cpm(self) -> float:
        return self.cpm_x10 / 10.0

    @property
    def cal_nsvh_per_cpm(self) -> float:
        return self.cal_q16 / 65536.0

    @property
    def has_histogram(self) -> bool:
        return bool(self.capabilities & NATIVE_CAPS_HISTOGRAM) and len(
            self.histogram_bins
        ) == 9

    @property
    def histogram_saturated(self) -> bool:
        return bool(self.histogram_flags & NATIVE_HISTOGRAM_SATURATED)

    def status_names(self) -> list[str]:
        return [name for bit, name in NATIVE_STATUS_BITS if self.status_lo & bit]


def decode_native(data: bytes) -> NativeReading:
    if len(data) < 0x1E:
        raise ValueError(f"native bank short: {len(data)} bytes")
    protocol = data[0x02]
    capabilities = data[0x03]
    window_s = 0
    holdoff_us = 0
    min_interval_us: Optional[int] = None
    histogram_bins: tuple[int, ...] = ()
    histogram_flags = 0
    if protocol >= 2 and len(data) >= 0x22:
        window_s = be16(data, 0x1E)
        holdoff_us = be16(data, 0x20)
    if (
        protocol >= 2
        and (capabilities & NATIVE_CAPS_HISTOGRAM)
        and len(data) >= 0x39
    ):
        min_interval_us = be32(data, 0x22)
        histogram_bins = tuple(be16(data, 0x26 + (index * 2)) for index in range(9))
        histogram_flags = data[0x38]
    return NativeReading(
        device_id=be16(data, 0x00),
        protocol=protocol,
        capabilities=capabilities,
        dose_nsvh=be32(data, 0x04),
        cpm_x10=be32(data, 0x08),
        # Big-endian uint16: 0x0C is the high byte (HV attempts), 0x0D is the flag byte.
        status_lo=data[0x0D],
        hv_attempts=data[0x0C],
        sequence=be32(data, 0x0E),
        cal_q16=be32(data, 0x12),
        count=be32(data, 0x16),
        elapsed_ms=be32(data, 0x1A),
        window_s=window_s,
        holdoff_us=holdoff_us,
        min_interval_us=min_interval_us,
        histogram_bins=histogram_bins,
        histogram_flags=histogram_flags,
    )


@dataclass
class RadSensReading:
    device_id: int
    firmware: int
    dynamic_x10: int
    static_x10: int
    hv_status: int
    hv_attempts: int
    i2c_address: int
    hv_generator: int
    sensitivity: int
    led_enable: int
    lp_mode: int = 0

    @property
    def dynamic_urh(self) -> float:
        return self.dynamic_x10 / 10.0

    @property
    def static_urh(self) -> float:
        return self.static_x10 / 10.0

    @property
    def dynamic_usvh(self) -> float:
        return self.dynamic_x10 / 1000.0

    @property
    def static_usvh(self) -> float:
        return self.static_x10 / 1000.0

    def hv_status_name(self) -> str:
        return RADSENS_HV_NAMES.get(self.hv_status, f"unknown({self.hv_status})")


def decode_radsens(
    id_fw: bytes,
    radiation: bytes,
    hv_byte: int,
    controls: bytes,
    lp_mode: int = 0,
) -> RadSensReading:
    if len(id_fw) < 2:
        raise ValueError("RadSens id short")
    if len(radiation) < 6:
        raise ValueError("RadSens radiation short")
    if len(controls) < 5:
        raise ValueError("RadSens controls short")
    return RadSensReading(
        device_id=id_fw[0],
        firmware=id_fw[1],
        dynamic_x10=be24(radiation, 0),
        static_x10=be24(radiation, 3),
        hv_status=hv_byte & 0x0F,
        hv_attempts=(hv_byte >> 4) & 0x0F,
        i2c_address=controls[0],
        hv_generator=controls[1],
        sensitivity=le16(controls, 2),
        led_enable=controls[4],
        lp_mode=lp_mode,
    )


def encode_native_calibration(q16: int) -> bytes:
    """Pointer 0x12 plus a non-zero big-endian Q16.16 coefficient."""
    if not 1 <= q16 <= 0xFFFFFFFF:
        raise ValueError("calibration must be a non-zero Q16.16 value")
    return bytes([0x12]) + q16.to_bytes(4, "big")


def encode_native_window(seconds: int) -> bytes:
    if not 1 <= seconds <= 400:
        raise ValueError("window must be 1..400 s")
    return bytes([0x1E, (seconds >> 8) & 0xFF, seconds & 0xFF])


def encode_native_holdoff(microseconds: int) -> bytes:
    if not 0 <= microseconds <= 300:
        raise ValueError("holdoff must be 0..300 µs")
    return bytes([0x20, (microseconds >> 8) & 0xFF, microseconds & 0xFF])


def encode_radsens_byte(pointer: int, value: int) -> bytes:
    if value not in (0, 1):
        raise ValueError("value must be 0 or 1")
    if pointer not in (0x0C, 0x11, 0x14):
        raise ValueError(f"not a one-byte RadSens control: 0x{pointer:02X}")
    return bytes([pointer, value])


def encode_radsens_address(address: int) -> bytes:
    if not 0x03 <= address <= 0x77:
        raise ValueError("address must be 0x03..0x77")
    return bytes([0x10, address])


def encode_radsens_sensitivity(impulses_per_ur: int) -> bytes:
    if not 0 <= impulses_per_ur <= 510:
        raise ValueError("sensitivity must be 0..510")
    return bytes([0x12, impulses_per_ur & 0xFF, (impulses_per_ur >> 8) & 0xFF])


def render_csv(history: SampleHistory, native: Optional[NativeReading] = None) -> str:
    """Time series, then the latest inter-arrival histogram when the map has one."""
    names = list(history.series)
    by_time: dict[float, dict[str, float]] = {}
    for name, points in history.series.items():
        for elapsed, value in points:
            by_time.setdefault(elapsed, {})[name] = value
    buf = io.StringIO()
    writer = csv.writer(buf)
    writer.writerow(["elapsed_s", *names])
    for elapsed in sorted(by_time):
        row: list[object] = [f"{elapsed:.3f}"]
        values = by_time[elapsed]
        for name in names:
            if name in values:
                row.append(f"{values[name]:.6g}")
            else:
                row.append("")
        writer.writerow(row)
    if native is not None and native.has_histogram:
        writer.writerow([])
        writer.writerow(["histogram_bin", "count"])
        for label, count in zip(NATIVE_HISTOGRAM_BIN_LABELS, native.histogram_bins):
            writer.writerow([label, count])
        gap = "" if native.min_interval_us in (None, 0xFFFFFFFF) else native.min_interval_us
        writer.writerow(["min_gap_us", gap])
        writer.writerow(["saturated", int(native.histogram_saturated)])
    return buf.getvalue()


# ---------------------------------------------------------------------------
# BME680 (Bosch integer compensation)
# ---------------------------------------------------------------------------

LOOKUP_TABLE_1 = (
    2147483647, 2147483647, 2147483647, 2147483647,
    2147483647, 2126008810, 2147483647, 2130303777,
    2147483647, 2147483647, 2143188679, 2136746228,
    2147483647, 2126008810, 2147483647, 2147483647,
)
LOOKUP_TABLE_2 = (
    4096000000, 2048000000, 1024000000, 512000000,
    255744255, 127110228, 64000000, 32258064,
    16016016, 8000000, 4000000, 2000000,
    1000000, 500000, 250000, 125000,
)


@dataclass
class Bme680Calib:
    par_t1: int = 0
    par_t2: int = 0
    par_t3: int = 0
    par_p1: int = 0
    par_p2: int = 0
    par_p3: int = 0
    par_p4: int = 0
    par_p5: int = 0
    par_p6: int = 0
    par_p7: int = 0
    par_p8: int = 0
    par_p9: int = 0
    par_p10: int = 0
    par_h1: int = 0
    par_h2: int = 0
    par_h3: int = 0
    par_h4: int = 0
    par_h5: int = 0
    par_h6: int = 0
    par_h7: int = 0
    par_gh1: int = 0
    par_gh2: int = 0
    par_gh3: int = 0
    res_heat_range: int = 0
    res_heat_val: int = 0
    range_sw_err: int = 0
    t_fine: int = 0


@dataclass
class Bme680Reading:
    temperature_c: float
    pressure_hpa: float
    humidity_rh: float
    gas_ohms: float
    gas_valid: bool
    heat_stable: bool
    new_data: bool


def parse_bme680_calib(
    coeff: bytes,
    res_heat_range_reg: int,
    res_heat_val: int,
    range_sw_err_reg: int,
) -> Bme680Calib:
    """coeff is 25 bytes from 0x89 followed by 16 bytes from 0xE1."""
    if len(coeff) < 41:
        raise ValueError(f"BME680 calib short: {len(coeff)}")
    c = coeff
    cal = Bme680Calib()
    cal.par_t2 = struct.unpack_from("<h", c, 1)[0]
    cal.par_t3 = u8_signed(c[3])
    cal.par_p1 = struct.unpack_from("<H", c, 5)[0]
    cal.par_p2 = struct.unpack_from("<h", c, 7)[0]
    cal.par_p3 = u8_signed(c[9])
    cal.par_p4 = struct.unpack_from("<h", c, 11)[0]
    cal.par_p5 = struct.unpack_from("<h", c, 13)[0]
    cal.par_p7 = u8_signed(c[15])
    cal.par_p6 = u8_signed(c[16])
    cal.par_p8 = struct.unpack_from("<h", c, 19)[0]
    cal.par_p9 = struct.unpack_from("<h", c, 21)[0]
    cal.par_p10 = c[23]
    cal.par_h2 = (c[25] << 4) | (c[26] >> 4)
    cal.par_h1 = (c[27] << 4) | (c[26] & 0x0F)
    cal.par_h3 = u8_signed(c[28])
    cal.par_h4 = u8_signed(c[29])
    cal.par_h5 = u8_signed(c[30])
    cal.par_h6 = c[31]
    cal.par_h7 = u8_signed(c[32])
    cal.par_t1 = struct.unpack_from("<H", c, 33)[0]
    cal.par_gh2 = struct.unpack_from("<h", c, 35)[0]
    cal.par_gh1 = u8_signed(c[37])
    cal.par_gh3 = u8_signed(c[38])
    cal.res_heat_range = (res_heat_range_reg & 0x30) // 16
    cal.res_heat_val = u8_signed(res_heat_val & 0xFF)
    cal.range_sw_err = u8_signed(range_sw_err_reg & 0xF0) // 16
    return cal


def calc_temperature(temp_adc: int, cal: Bme680Calib) -> int:
    """Returns temperature in 0.01 °C and updates cal.t_fine."""
    var1 = (temp_adc >> 3) - (cal.par_t1 << 1)
    var2 = (var1 * cal.par_t2) >> 11
    var3 = ((var1 >> 1) * (var1 >> 1)) >> 12
    var3 = (var3 * (cal.par_t3 << 4)) >> 14
    cal.t_fine = var2 + var3
    return ((cal.t_fine * 5) + 128) >> 8


def calc_pressure(pres_adc: int, cal: Bme680Calib) -> int:
    """Returns pressure in Pa."""
    var1 = (cal.t_fine >> 1) - 64000
    var2 = ((((var1 >> 2) * (var1 >> 2)) >> 11) * cal.par_p6) >> 2
    var2 = var2 + ((var1 * cal.par_p5) << 1)
    var2 = (var2 >> 2) + (cal.par_p4 << 16)
    var1 = (
        (((((var1 >> 2) * (var1 >> 2)) >> 13) * (cal.par_p3 << 5)) >> 3)
        + ((cal.par_p2 * var1) >> 1)
    )
    var1 = var1 >> 18
    var1 = ((32768 + var1) * cal.par_p1) >> 15
    pressure_comp = 1048576 - pres_adc
    pressure_comp = (pressure_comp - (var2 >> 12)) * 3125
    if var1 == 0:
        return 0
    if pressure_comp >= 0x40000000:
        pressure_comp = (pressure_comp // var1) << 1
    else:
        pressure_comp = (pressure_comp << 1) // var1
    var1 = (cal.par_p9 * (((pressure_comp >> 3) * (pressure_comp >> 3)) >> 13)) >> 12
    var2 = ((pressure_comp >> 2) * cal.par_p8) >> 13
    var3 = (
        ((pressure_comp >> 8) * (pressure_comp >> 8) * (pressure_comp >> 8) * cal.par_p10)
        >> 17
    )
    pressure_comp = pressure_comp + ((var1 + var2 + var3 + (cal.par_p7 << 7)) >> 4)
    return pressure_comp & 0xFFFFFFFF


def calc_humidity(hum_adc: int, cal: Bme680Calib) -> int:
    """Returns humidity in 0.001 %RH."""
    temp_scaled = ((cal.t_fine * 5) + 128) >> 8
    var1 = (
        hum_adc
        - (cal.par_h1 * 16)
        - (((temp_scaled * cal.par_h3) // 100) >> 1)
    )
    var2 = (
        cal.par_h2
        * (
            ((temp_scaled * cal.par_h4) // 100)
            + ((((temp_scaled * ((temp_scaled * cal.par_h5) // 100)) >> 6) // 100))
            + (1 << 14)
        )
    ) >> 10
    var3 = var1 * var2
    var4 = cal.par_h6 << 7
    var4 = (var4 + ((temp_scaled * cal.par_h7) // 100)) >> 4
    var5 = ((var3 >> 14) * (var3 >> 14)) >> 10
    var6 = (var4 * var5) >> 1
    calc_hum = (((var3 + var6) >> 10) * 1000) >> 12
    if calc_hum > 100000:
        calc_hum = 100000
    elif calc_hum < 0:
        calc_hum = 0
    return calc_hum


def calc_gas_resistance(gas_adc: int, gas_range: int, cal: Bme680Calib) -> int:
    """Returns gas resistance in ohms."""
    var1 = ((1340 + (5 * cal.range_sw_err)) * LOOKUP_TABLE_1[gas_range]) >> 16
    var2 = ((gas_adc << 15) - 16777216) + var1
    var3 = (LOOKUP_TABLE_2[gas_range] * var1) >> 9
    if var2 == 0:
        return 0
    return (var3 + (var2 >> 1)) // var2


def calc_heater_res(temp: int, amb_temp: int, cal: Bme680Calib) -> int:
    if temp < 200:
        temp = 200
    elif temp > 400:
        temp = 400
    var1 = ((amb_temp * cal.par_gh3) // 1000) * 256
    var2 = (cal.par_gh1 + 784) * (
        ((((cal.par_gh2 + 154009) * temp * 5) // 100) + 3276800) // 10
    )
    var3 = var1 + (var2 // 2)
    var4 = var3 // (cal.res_heat_range + 4)
    var5 = (131 * cal.res_heat_val) + 65536
    heatr_res_x100 = ((var4 // var5) - 250) * 34
    return ((heatr_res_x100 + 50) // 100) & 0xFF


def calc_heater_dur(dur_ms: int) -> int:
    if dur_ms >= 0xFC0:
        return 0xFF
    factor = 0
    dur = dur_ms
    while dur > 0x3F:
        dur //= 4
        factor += 1
    return (dur + (factor * 64)) & 0xFF


def compensate_field(field: bytes, cal: Bme680Calib) -> Bme680Reading:
    if len(field) < 15:
        raise ValueError(f"BME680 field short: {len(field)}")
    status = field[0]
    adc_pres = (field[2] << 12) | (field[3] << 4) | (field[4] >> 4)
    adc_temp = (field[5] << 12) | (field[6] << 4) | (field[7] >> 4)
    adc_hum = (field[8] << 8) | field[9]
    adc_gas = (field[13] << 2) | (field[14] >> 6)
    gas_range = field[14] & 0x0F
    gas_valid = bool(field[14] & 0x20)
    heat_stable = bool(field[14] & 0x10)
    new_data = bool(status & 0x80)

    temp_x100 = calc_temperature(adc_temp, cal)
    pressure_pa = calc_pressure(adc_pres, cal)
    humidity_x1000 = calc_humidity(adc_hum, cal)
    gas_ohms = calc_gas_resistance(adc_gas, gas_range, cal) if gas_valid else 0

    return Bme680Reading(
        temperature_c=temp_x100 / 100.0,
        pressure_hpa=pressure_pa / 100.0,
        humidity_rh=humidity_x1000 / 1000.0,
        gas_ohms=float(gas_ohms),
        gas_valid=gas_valid,
        heat_stable=heat_stable,
        new_data=new_data,
    )


# ---------------------------------------------------------------------------
# Serial bridge client
# ---------------------------------------------------------------------------

class BridgeError(RuntimeError):
    pass


class BridgeClient:
    def __init__(self, port: str, baud: int = BAUD, timeout: float = BRIDGE_TIMEOUT_S):
        self.port = port
        self.baud = baud
        self.timeout = timeout
        self._ser: Optional[serial.Serial] = None
        self._warmup_done = False

    def open(self) -> None:
        self._ser = serial.Serial(self.port, self.baud, timeout=self.timeout)
        time.sleep(0.3)
        self._ser.reset_input_buffer()
        # Discard banner / leftover lines.
        deadline = time.monotonic() + 1.0
        while time.monotonic() < deadline:
            line = self._readline()
            if line is None:
                break
        self._warmup_done = False

    def close(self) -> None:
        if self._ser is not None:
            try:
                self._ser.close()
            finally:
                self._ser = None

    def _readline(self) -> Optional[str]:
        assert self._ser is not None
        raw = self._ser.readline()
        if not raw:
            return None
        return raw.decode("utf-8", errors="replace").rstrip("\r\n")

    def _write_line(self, cmd: str) -> None:
        assert self._ser is not None
        self._ser.write((cmd + "\n").encode("ascii"))
        self._ser.flush()

    def _collect_response(self, done) -> list[str]:
        """Read lines until done(lines) is true or the timeout expires."""
        lines: list[str] = []
        assert self._ser is not None
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            line = self._readline()
            if line is None:
                if lines and done(lines):
                    break
                continue
            if line:
                lines.append(line)
            if done(lines):
                # Drain a short quiet window for trailing lines of the same reply.
                quiet_deadline = time.monotonic() + 0.04
                old_timeout = self._ser.timeout
                self._ser.timeout = 0.04
                try:
                    while time.monotonic() < quiet_deadline:
                        extra = self._readline()
                        if extra:
                            lines.append(extra)
                            quiet_deadline = time.monotonic() + 0.04
                        else:
                            break
                finally:
                    self._ser.timeout = old_timeout
                break
        return lines

    def _transact(self, cmd: str, done) -> list[str]:
        self._write_line(cmd)
        return self._collect_response(done)

    def _with_warmup(self, fn):
        try:
            return fn()
        except BridgeError:
            if self._warmup_done:
                raise
            self._warmup_done = True
            time.sleep(0.1)
            return fn()

    def scan(self) -> list[int]:
        def once() -> list[int]:
            lines = self._transact(
                "s",
                lambda ls: any(ln.startswith("Found ") for ln in ls)
                or any(ln.startswith("ERROR") for ln in ls),
            )
            addrs: list[int] = []
            for line in lines:
                m = re.match(r"\s*0x([0-9A-Fa-f]{2})\s*$", line)
                if m:
                    addrs.append(int(m.group(1), 16))
            if not any(ln.startswith("Found ") for ln in lines):
                raise BridgeError(f"scan failed: {lines!r}")
            return addrs

        return self._with_warmup(once)

    def xfer(self, addr: int, write_bytes: list[int], length: int) -> bytes:
        """Repeated-START write+read: x ADDR B0... / LEN"""
        if length < 1 or length > BRIDGE_MAX_TRANSFER:
            raise BridgeError(
                f"bridge read length {length} is outside 1..{BRIDGE_MAX_TRANSFER}"
            )

        def once() -> bytes:
            parts = [f"{b:02X}" for b in write_bytes]
            cmd = f"x {addr:02X} {' '.join(parts)} / {length}"
            lines = self._transact(
                cmd,
                lambda ls: any(ln.startswith("DATA:") for ln in ls)
                or any(ln.startswith("ERROR") for ln in ls)
                or any(
                    ln.startswith("WRITE") and "OK" not in ln for ln in ls
                ),
            )
            return self._parse_xfer(lines, addr, length)

        return self._with_warmup(once)

    def write(self, addr: int, data: list[int]) -> None:
        def once() -> None:
            parts = [f"{b:02X}" for b in data]
            cmd = f"w {addr:02X} {' '.join(parts)}"
            lines = self._transact(
                cmd,
                lambda ls: any(ln.startswith("WRITE") for ln in ls)
                or any(ln.startswith("ERROR") for ln in ls),
            )
            for line in lines:
                if line.startswith("WRITE"):
                    if "OK" not in line:
                        raise BridgeError(line)
                    return
                if line.startswith("ERROR"):
                    raise BridgeError(line)
            raise BridgeError(f"write got no WRITE line: {lines!r}")

        self._with_warmup(once)

    @staticmethod
    def _parse_xfer(lines: list[str], addr: int, length: int) -> bytes:
        data: Optional[bytes] = None
        write_ok = False
        for line in lines:
            if line.startswith("ERROR"):
                raise BridgeError(line)
            if line.startswith("WRITE"):
                if "OK" not in line:
                    raise BridgeError(line)
                write_ok = True
            if line.startswith("DATA:"):
                hex_part = line[5:].strip()
                if not hex_part:
                    data = b""
                else:
                    data = bytes(int(x, 16) for x in hex_part.split())
        if not write_ok:
            raise BridgeError(f"missing WRITE OK for 0x{addr:02X}: {lines!r}")
        if data is None:
            raise BridgeError(f"missing DATA for 0x{addr:02X}: {lines!r}")
        if len(data) != length:
            raise BridgeError(
                f"short read 0x{addr:02X}: got {len(data)}, want {length}: {lines!r}"
            )
        return data


def default_port() -> Optional[str]:
    matches = sorted(glob.glob(DEFAULT_PORT_GLOB))
    return matches[0] if matches else None


# ---------------------------------------------------------------------------
# Device polling
# ---------------------------------------------------------------------------

@dataclass
class RegisterWrite:
    i2c_addr: int
    payload: bytes
    summary: str
    new_radsens_addr: Optional[int] = None


@dataclass
class Snapshot:
    port: str
    scanned: list[int] = field(default_factory=list)
    mode: str = "none"  # native | radsens | none
    native: Optional[NativeReading] = None
    radsens: Optional[RadSensReading] = None
    bme: Optional[Bme680Reading] = None
    bme_present: bool = False
    error: str = ""
    notice: str = ""


class DeviceSession:
    def __init__(self, client: BridgeClient):
        self.client = client
        self.mode = "none"
        self.bme_present = False
        self.bme_calib: Optional[Bme680Calib] = None
        self.bme_heater: int = 0
        self.bme_wait: int = 0
        self.amb_temp = 25
        self.radsens_addr = RADSENS_ADDR

    def identify(self) -> list[int]:
        scanned = self.client.scan()
        self.mode = "none"
        self.bme_present = False
        self.bme_calib = None

        if NATIVE_ADDR in scanned:
            try:
                data = self.client.xfer(NATIVE_ADDR, [0x00], 2)
                if be16(data) == NATIVE_ID:
                    self.mode = "native"
            except BridgeError:
                pass
        if self.mode == "none" and self._claim_radsens(scanned):
            self.mode = "radsens"

        if BME_ADDR in scanned:
            try:
                chip = self.client.xfer(BME_ADDR, [0xD0], 1)
                if chip[0] == BME_CHIP_ID:
                    self.bme_present = True
                    self._init_bme()
            except BridgeError:
                self.bme_present = False
        return scanned

    def _claim_radsens(self, scanned: list[int]) -> bool:
        candidates = [addr for addr in scanned if addr not in (NATIVE_ADDR, BME_ADDR)]
        if RADSENS_ADDR in candidates:
            candidates.remove(RADSENS_ADDR)
            candidates.insert(0, RADSENS_ADDR)
        for addr in candidates:
            try:
                data = self.client.xfer(addr, [0x00], 1)
            except BridgeError:
                continue
            if data and data[0] == RADSENS_ID:
                self.radsens_addr = addr
                return True
        return False

    def _init_bme(self) -> None:
        coeff1 = self.client.xfer(BME_ADDR, [0x89], 25)
        coeff2 = self.client.xfer(BME_ADDR, [0xE1], 16)
        rhr = self.client.xfer(BME_ADDR, [0x02], 1)[0]
        rhv = self.client.xfer(BME_ADDR, [0x00], 1)[0]
        rse = self.client.xfer(BME_ADDR, [0x04], 1)[0]
        self.bme_calib = parse_bme680_calib(coeff1 + coeff2, rhr, rhv, rse)
        self.bme_heater = calc_heater_res(300, self.amb_temp, self.bme_calib)
        self.bme_wait = calc_heater_dur(100)

    def poll(self) -> Snapshot:
        snap = Snapshot(port=self.client.port, mode=self.mode, bme_present=self.bme_present)
        try:
            if self.mode == "native":
                # The bridge rejects a single 57-byte read. Each span sets the pointer.
                parts = [
                    self.client.xfer(NATIVE_ADDR, [offset], length)
                    for offset, length in read_spans(NATIVE_MAP_SIZE)
                ]
                snap.native = decode_native(b"".join(parts))
            elif self.mode == "radsens":
                # Never read 0x09/0x0A and never burst through them.
                addr = self.radsens_addr
                id_fw = self.client.xfer(addr, [0x00], 2)
                radiation = self.client.xfer(addr, [0x03], 6)
                hv = self.client.xfer(addr, [0x0B], 1)[0]
                lp_mode = self.client.xfer(addr, [0x0C], 1)[0]
                controls = self.client.xfer(addr, [0x10], 5)
                snap.radsens = decode_radsens(id_fw, radiation, hv, controls, lp_mode)

            if self.bme_present and self.bme_calib is not None:
                snap.bme = self._poll_bme()
        except BridgeError as exc:
            snap.error = str(exc)
        return snap

    def _poll_bme(self) -> Bme680Reading:
        assert self.bme_calib is not None
        # ctrl_hum = os_hum 1x; heat enable; run_gas + nb_conv 0; then forced measure.
        # os_temp=2, os_pres=3 (4x), mode=forced → 0x4D
        self.client.write(BME_ADDR, [0x72, 0x01])  # ctrl_hum
        self.client.write(BME_ADDR, [0x5A, self.bme_heater])  # res_heat0
        self.client.write(BME_ADDR, [0x64, self.bme_wait])  # gas_wait0
        self.client.write(BME_ADDR, [0x70, 0x00])  # heat ctrl enable
        self.client.write(BME_ADDR, [0x71, 0x10])  # run_gas, nb_conv=0
        self.client.write(BME_ADDR, [0x74, 0x4D])  # ctrl_meas forced

        # Wait for conversion + 100 ms heater.
        time.sleep(0.20)
        last = None
        for _ in range(10):
            field = self.client.xfer(BME_ADDR, [0x1D], 15)
            reading = compensate_field(field, self.bme_calib)
            last = reading
            if reading.new_data:
                self.amb_temp = int(round(reading.temperature_c))
                return reading
            time.sleep(0.05)
        if last is None:
            raise BridgeError("BME680 no field data")
        return last


# ---------------------------------------------------------------------------
# In-memory history
# ---------------------------------------------------------------------------

class SampleHistory:
    """Elapsed-time series kept in RAM. Nothing is written to disk."""

    def __init__(self, maxlen: int = HISTORY_MAX):
        self.maxlen = maxlen
        self.t0: Optional[float] = None
        self.series: dict[str, deque[tuple[float, float]]] = {}

    def add(self, name: str, value: float, when: Optional[float] = None) -> None:
        if when is None:
            when = time.time()
        if self.t0 is None:
            self.t0 = when
        buf = self.series.get(name)
        if buf is None or buf.maxlen != self.maxlen:
            buf = deque(self.series.get(name, ()), maxlen=self.maxlen)
            self.series[name] = buf
        buf.append((when - self.t0, float(value)))

    def clear(self) -> None:
        self.series.clear()
        self.t0 = None

    def points(self, name: str) -> list[tuple[float, float]]:
        return list(self.series.get(name, ()))

    def sample_count(self) -> int:
        if not self.series:
            return 0
        return max(len(buf) for buf in self.series.values())


def record_snapshot(history: SampleHistory, snap: Snapshot, when: Optional[float] = None) -> None:
    """Append one poll. Skips a channel that did not produce a new reading."""
    if when is None:
        when = time.time()
    if snap.native is not None:
        history.add("dose_usvh", snap.native.dose_usvh, when)
        history.add("cpm", snap.native.cpm, when)
    if snap.radsens is not None:
        history.add("dynamic_usvh", snap.radsens.dynamic_usvh, when)
        history.add("static_usvh", snap.radsens.static_usvh, when)
        if snap.radsens.sensitivity:
            # µR/h = CPM * 60 / S  →  CPM = µR/h * S / 60
            history.add(
                "dynamic_cpm",
                snap.radsens.dynamic_urh * snap.radsens.sensitivity / 60.0,
                when,
            )
            history.add(
                "static_cpm",
                snap.radsens.static_urh * snap.radsens.sensitivity / 60.0,
                when,
            )
    if snap.bme is not None and snap.bme.new_data:
        history.add("temperature_c", snap.bme.temperature_c, when)
        history.add("pressure_hpa", snap.bme.pressure_hpa, when)
        history.add("humidity_rh", snap.bme.humidity_rh, when)
        if snap.bme.gas_valid:
            history.add("gas_kohm", snap.bme.gas_ohms / 1000.0, when)


def padded_range(lo: float, hi: float) -> tuple[float, float]:
    if lo == hi:
        pad = abs(lo) * 0.05 or 1.0
        return lo - pad, hi + pad
    pad = (hi - lo) * 0.08
    return lo - pad, hi + pad


def format_elapsed(seconds: float) -> str:
    whole = max(0, int(seconds))
    if whole < 90:
        return f"{whole}s"
    if whole < 90 * 60:
        return f"{whole // 60}m"
    return f"{whole / 3600.0:.1f}h"


def format_y(value: float) -> str:
    mag = abs(value)
    if mag >= 100:
        return f"{value:.0f}"
    if mag >= 10:
        return f"{value:.1f}"
    if mag >= 1:
        return f"{value:.2f}"
    return f"{value:.3f}"


def format_elapsed_precise(seconds: float) -> str:
    whole = max(0, int(round(seconds)))
    minutes, secs = divmod(whole, 60)
    hours, minutes = divmod(minutes, 60)
    if hours:
        return f"{hours}h {minutes:02d}m {secs:02d}s"
    if minutes:
        return f"{minutes}m {secs:02d}s"
    return f"{secs}s"


CHART_MARGIN = (56, 24, 12, 22)


def series_bounds(series: list[tuple[str, object, list[tuple[float, float]]]]) -> tuple[float, float, float, float]:
    xs = [p[0] for _, _, pts in series for p in pts]
    ys = [p[1] for _, _, pts in series for p in pts]
    xmin, xmax = min(xs), max(xs)
    if xmax <= xmin:
        xmax = xmin + 1.0
    ymin, ymax = padded_range(min(ys), max(ys))
    return xmin, xmax, ymin, ymax


def nearest_point(points: list[tuple[float, float]], t: float) -> tuple[float, float]:
    """Closest sample. Points are in time order. An equal gap keeps the earlier sample."""
    if not points:
        raise ValueError("empty series")
    lo, hi = 0, len(points)
    while lo < hi:
        mid = (lo + hi) // 2
        if points[mid][0] < t:
            lo = mid + 1
        else:
            hi = mid
    candidates = []
    if lo < len(points):
        candidates.append(points[lo])
    if lo > 0:
        candidates.append(points[lo - 1])
    return min(candidates, key=lambda p: (abs(p[0] - t), p[0]))


def time_from_x(px: float, plot_left: float, plot_width: float, xmin: float, xmax: float) -> float:
    if plot_width <= 0:
        return xmin
    return xmin + (px - plot_left) / plot_width * (xmax - xmin)


def x_from_time(t: float, plot_left: float, plot_width: float, xmin: float, xmax: float) -> float:
    if xmax <= xmin:
        return plot_left
    return plot_left + (t - xmin) / (xmax - xmin) * plot_width


class HistoryChart(QWidget):
    """One line chart over SampleHistory. Traces with no points are omitted."""

    def __init__(self, title: str, ylabel: str, traces: list[tuple[str, str, QColor]]):
        super().__init__()
        self.title = title
        self.ylabel = ylabel
        self.traces = traces  # (series name, legend, color)
        self.history: Optional[SampleHistory] = None
        self.cursor_t: Optional[float] = None
        self.show_readout = False
        self.hover_callback = None
        self.setMinimumHeight(160)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
        self.setMouseTracking(True)

    def set_history(self, history: SampleHistory) -> None:
        self.history = history

    def _visible_series(self) -> list[tuple[str, QColor, list[tuple[float, float]]]]:
        series = []
        if self.history is None:
            return series
        for name, legend, color in self.traces:
            pts = self.history.points(name)
            if pts:
                series.append((legend, color, pts))
        return series

    def _plot_rect(self):
        left, top, right, bottom = CHART_MARGIN
        return self.rect().adjusted(left, top, -right, -bottom)

    def _time_at(self, px: float) -> Optional[float]:
        series = self._visible_series()
        plot = self._plot_rect()
        if not series or plot.width() < 10 or px < plot.left() or px > plot.right():
            return None
        xmin, xmax, _, _ = series_bounds(series)
        t = time_from_x(px, plot.left(), plot.width(), xmin, xmax)
        candidates = [nearest_point(pts, t) for _, _, pts in series]
        snapped = min(candidates, key=lambda p: (abs(p[0] - t), p[0]))
        return snapped[0]

    def mouseMoveEvent(self, event) -> None:  # noqa: N802
        t = self._time_at(event.position().x())
        if self.hover_callback is not None:
            self.hover_callback(self, t)
        else:
            self.cursor_t = t
            self.show_readout = t is not None
            self.update()
        self.setCursor(
            Qt.CursorShape.CrossCursor if t is not None else Qt.CursorShape.ArrowCursor
        )

    def leaveEvent(self, event) -> None:  # noqa: N802
        del event
        self.setCursor(Qt.CursorShape.ArrowCursor)
        if self.hover_callback is not None:
            self.hover_callback(self, None)
        else:
            self.cursor_t = None
            self.show_readout = False
            self.update()

    def paintEvent(self, event) -> None:  # noqa: N802
        del event
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        palette = self.palette()
        painter.fillRect(self.rect(), palette.color(QPalette.ColorRole.Base))
        fg = palette.color(QPalette.ColorRole.Text)
        grid = palette.color(QPalette.ColorRole.Mid)
        painter.setPen(fg)
        painter.drawText(8, 16, self.title)

        plot = self._plot_rect()
        if plot.width() < 10 or plot.height() < 10:
            painter.end()
            return

        series = self._visible_series()
        if not series:
            painter.drawText(plot, Qt.AlignmentFlag.AlignCenter, "waiting for samples")
            painter.end()
            return

        xmin, xmax, ymin, ymax = series_bounds(series)

        painter.setPen(QPen(grid, 1))
        for i in range(5):
            y = plot.top() + plot.height() * i / 4
            painter.drawLine(plot.left(), int(y), plot.right(), int(y))
            value = ymax - (ymax - ymin) * i / 4
            painter.setPen(fg)
            painter.drawText(2, int(y) + 4, format_y(value))
            painter.setPen(QPen(grid, 1))

        painter.setPen(fg)
        painter.drawText(plot.left(), self.height() - 4, format_elapsed(xmin))
        painter.drawText(plot.right() - 36, self.height() - 4, format_elapsed(xmax))
        painter.drawText(plot.right() - 88, 16, self.ylabel)

        def map_point(t: float, value: float) -> QPointF:
            x = x_from_time(t, plot.left(), plot.width(), xmin, xmax)
            y = plot.bottom() - (value - ymin) / (ymax - ymin) * plot.height()
            return QPointF(x, y)

        for _legend, color, pts in series:
            painter.setPen(QPen(color, 2))
            mapped = [map_point(t, value) for t, value in pts]
            if len(mapped) == 1:
                painter.drawEllipse(mapped[0], 2.5, 2.5)
            else:
                for a, b in zip(mapped, mapped[1:]):
                    painter.drawLine(a, b)

        cursor_samples: list[tuple[str, QColor, float]] = []
        if self.cursor_t is not None and xmin <= self.cursor_t <= xmax:
            cx = x_from_time(self.cursor_t, plot.left(), plot.width(), xmin, xmax)
            pen = QPen(fg, 1)
            pen.setStyle(Qt.PenStyle.DashLine)
            painter.setPen(pen)
            painter.drawLine(int(cx), plot.top(), int(cx), plot.bottom())
            for legend, color, pts in series:
                sample_t, value = nearest_point(pts, self.cursor_t)
                cursor_samples.append((legend, color, value))
                painter.setPen(QPen(color, 2))
                painter.setBrush(color)
                painter.drawEllipse(map_point(sample_t, value), 3.5, 3.5)
            painter.setBrush(Qt.BrushStyle.NoBrush)

        legend_x = plot.left() + 6
        painter.setPen(fg)
        for legend, color, _pts in series:
            painter.setPen(color)
            painter.drawText(legend_x, plot.top() + 12, legend)
            legend_x += painter.fontMetrics().horizontalAdvance(legend) + 14

        if self.show_readout and cursor_samples:
            lines = [format_elapsed_precise(self.cursor_t)]
            for legend, _color, value in cursor_samples:
                lines.append(f"{legend}  {format_y(value)} {self.ylabel}")
            metrics = painter.fontMetrics()
            width = max(metrics.horizontalAdvance(line) for line in lines) + 12
            height = metrics.height() * len(lines) + 8
            cx = x_from_time(self.cursor_t, plot.left(), plot.width(), xmin, xmax)
            box_x = cx + 8
            if box_x + width > plot.right():
                box_x = cx - width - 8
            box_y = plot.top() + 18
            painter.fillRect(int(box_x), int(box_y), int(width), int(height), palette.color(QPalette.ColorRole.Base))
            painter.setPen(fg)
            painter.drawRect(int(box_x), int(box_y), int(width), int(height))
            text_y = int(box_y) + metrics.ascent() + 4
            for line in lines:
                painter.drawText(int(box_x) + 6, text_y, line)
                text_y += metrics.height()

        painter.end()


# ---------------------------------------------------------------------------
# Qt UI
# ---------------------------------------------------------------------------

class HistogramBars(QWidget):
    """Horizontal bars for the nine native inter-arrival bins."""

    def __init__(self):
        super().__init__()
        self.counts: tuple[int, ...] = ()
        self.saturated = False
        self.caption = ""
        self.setMinimumHeight(188)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)

    def set_bins(self, counts: tuple[int, ...], min_gap: Optional[int], saturated: bool) -> None:
        self.counts = counts
        self.saturated = saturated
        if min_gap in (None, 0xFFFFFFFF):
            gap = "none yet"
        else:
            gap = f"{min_gap} µs"
        note = "saturated" if saturated else "this RUN, not the dose window"
        self.caption = f"Shortest accepted gap: {gap}  ({note})"
        self.update()

    def paintEvent(self, event) -> None:  # noqa: N802
        del event
        painter = QPainter(self)
        palette = self.palette()
        painter.fillRect(self.rect(), palette.color(QPalette.ColorRole.Base))
        fg = palette.color(QPalette.ColorRole.Text)
        bar = QColor("#3d9eff")
        painter.setPen(fg)
        painter.drawText(8, 16, self.caption or "Inter-arrival histogram")
        if len(self.counts) != 9:
            painter.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter, "no histogram")
            painter.end()
            return
        top = 24
        row_h = max(14, (self.height() - top - 6) // 9)
        label_w = 92
        count_w = 52
        max_count = max(self.counts) or 1
        track_left = label_w + 8
        track_right = self.width() - count_w - 8
        track_w = max(10, track_right - track_left)
        for index, (label, count) in enumerate(zip(NATIVE_HISTOGRAM_BIN_LABELS, self.counts)):
            y = top + index * row_h
            painter.setPen(fg)
            painter.drawText(4, y + row_h - 4, label)
            span = int(track_w * count / max_count)
            painter.fillRect(track_left, y + 3, span, row_h - 6, bar)
            painter.drawText(track_right + 6, y + row_h - 4, str(count))
        painter.end()


class PollWorker(QThread):
    snapshot = pyqtSignal(object)
    failed = pyqtSignal(str)

    def __init__(self, port: str, write_queue: "queue.Queue[RegisterWrite]"):
        super().__init__()
        self.port = port
        self.write_queue = write_queue
        self._stop = False

    def stop(self) -> None:
        self._stop = True

    def _drain_writes(self, session: DeviceSession) -> str:
        notes: list[str] = []
        while True:
            try:
                cmd = self.write_queue.get_nowait()
            except queue.Empty:
                break
            try:
                session.client.write(cmd.i2c_addr, list(cmd.payload))
                if cmd.new_radsens_addr is not None:
                    session.radsens_addr = cmd.new_radsens_addr
                notes.append(cmd.summary)
            except BridgeError as exc:
                notes.append(f"Write failed: {exc}")
        return "; ".join(notes)

    def run(self) -> None:
        client = BridgeClient(self.port)
        try:
            client.open()
        except Exception as exc:  # noqa: BLE001
            self.failed.emit(f"open {self.port}: {exc}")
            return
        session = DeviceSession(client)
        try:
            scanned = session.identify()
            first = Snapshot(
                port=self.port,
                scanned=scanned,
                mode=session.mode,
                bme_present=session.bme_present,
            )
            if session.mode == "none" and not session.bme_present:
                first.error = "no GeigerNode or BME680 on the bus"
            self.snapshot.emit(first)

            while not self._stop:
                t0 = time.monotonic()
                notice = self._drain_writes(session)
                snap = session.poll()
                snap.scanned = scanned
                snap.notice = notice
                self.snapshot.emit(snap)
                elapsed = time.monotonic() - t0
                remaining = POLL_INTERVAL_S - elapsed
                end = time.monotonic() + max(0.05, remaining)
                while not self._stop and time.monotonic() < end:
                    if not self.write_queue.empty():
                        break
                    time.sleep(0.05)
        except Exception as exc:  # noqa: BLE001
            self.failed.emit(str(exc))
        finally:
            client.close()


def dash(value: str = "—") -> str:
    return value


class MainWindow(QMainWindow):
    def __init__(self, port: str):
        super().__init__()
        self.setWindowTitle("GeigerNode I2C viewer")
        self.resize(980, 960)
        self.port = port
        self.worker: Optional[PollWorker] = None
        self.history = SampleHistory()
        self.write_queue: queue.Queue[RegisterWrite] = queue.Queue()
        self.writes_armed = False
        self.native_writable = False
        self._notice = ""
        self.last_native: Optional[NativeReading] = None
        self.last_radsens: Optional[RadSensReading] = None

        root = QWidget()
        self.setCentralWidget(root)
        layout = QVBoxLayout(root)

        conn = QHBoxLayout()
        self.port_edit = QLineEdit(port)
        self.connect_btn = QPushButton("Connect")
        self.connect_btn.clicked.connect(self.toggle_connect)
        conn.addWidget(QLabel("Port"))
        conn.addWidget(self.port_edit, stretch=1)
        conn.addWidget(self.connect_btn)
        layout.addLayout(conn)

        self.mode_label = QLabel("Disconnected")
        layout.addWidget(self.mode_label)

        self.rad_box = QGroupBox("Radiation")
        rad_row = QHBoxLayout(self.rad_box)
        meas_box = QGroupBox("Measurements")
        fixed_box = QGroupBox("Fixed")
        meas_layout = QVBoxLayout(meas_box)
        fixed_layout = QVBoxLayout(fixed_box)
        self.meas_form = QFormLayout()
        self.fixed_form = QFormLayout()
        meas_layout.addLayout(self.meas_form)
        self.histogram = HistogramBars()
        self.histogram.hide()
        meas_layout.addWidget(self.histogram)
        fixed_layout.addLayout(self.fixed_form)
        fixed_layout.addWidget(self._build_writes())
        rad_row.addWidget(meas_box, stretch=3)
        rad_row.addWidget(fixed_box, stretch=2)

        self.rad_labels: dict[str, QLabel] = {}
        self.field_form: dict[str, QFormLayout] = {}
        for form, rows in (
            (
                self.meas_form,
                (
                    ("dose", "Dose"),
                    ("cpm", "CPM"),
                    ("status", "Status"),
                    ("sequence", "Sequence"),
                    ("raw", "Count"),
                    ("elapsed", "Elapsed"),
                    ("min_gap", "Min gap"),
                    ("dynamic", "Dynamic"),
                    ("static", "Static"),
                    ("hv_status", "HV status"),
                ),
            ),
            (
                self.fixed_form,
                (
                    ("protocol", "Protocol"),
                    ("calibration", "Calibration"),
                    ("window", "Window"),
                    ("holdoff", "Holdoff"),
                    ("firmware", "Firmware"),
                    ("address", "Address"),
                    ("sensitivity", "Sensitivity"),
                    ("led", "LED"),
                    ("lp", "Low power"),
                    ("hv_generator", "HV generator"),
                ),
            ),
        ):
            for key, title in rows:
                lab = QLabel(dash())
                lab.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
                lab.setWordWrap(True)
                self.rad_labels[key] = lab
                self.field_form[key] = form
                form.addRow(title, lab)

        self.bme_box = QGroupBox("BME680")
        bme_form = QFormLayout(self.bme_box)
        self.bme_labels: dict[str, QLabel] = {}
        for key in ("temperature", "pressure", "humidity", "gas", "flags"):
            lab = QLabel(dash())
            lab.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
            self.bme_labels[key] = lab
            bme_form.addRow(key.title(), lab)

        graphs = QGroupBox("History (in memory)")
        graphs_layout = QVBoxLayout(graphs)
        header = QHBoxLayout()
        self.history_label = QLabel(self._history_caption())
        export_btn = QPushButton("Export CSV")
        export_btn.clicked.connect(self.export_csv)
        clear_btn = QPushButton("Clear history")
        clear_btn.clicked.connect(self.clear_history)
        header.addWidget(self.history_label, stretch=1)
        header.addWidget(export_btn)
        header.addWidget(clear_btn)
        graphs_layout.addLayout(header)

        self.charts = [
            HistoryChart(
                "Dose rate",
                "µSv/h",
                [
                    ("dose_usvh", "native", QColor("#3d9eff")),
                    ("dynamic_usvh", "dynamic", QColor("#ffb020")),
                    ("static_usvh", "static", QColor("#3dd68c")),
                ],
            ),
            HistoryChart(
                "Count rate",
                "CPM",
                [
                    ("cpm", "native", QColor("#c084fc")),
                    ("dynamic_cpm", "dynamic", QColor("#ffb020")),
                    ("static_cpm", "static", QColor("#3dd68c")),
                ],
            ),
            HistoryChart("Temperature", "°C", [("temperature_c", "BME680", QColor("#ff6b6b"))]),
            HistoryChart("Pressure", "hPa", [("pressure_hpa", "BME680", QColor("#4dabf7"))]),
            HistoryChart("Humidity", "%RH", [("humidity_rh", "BME680", QColor("#38d9a9"))]),
            HistoryChart("Gas", "kΩ", [("gas_kohm", "BME680", QColor("#fab005"))]),
        ]
        for chart in self.charts:
            chart.set_history(self.history)
            chart.hover_callback = self._on_chart_hover
        graphs_layout.addWidget(self.charts[0])
        graphs_layout.addWidget(self.charts[1])
        grid = QGridLayout()
        for index, chart in enumerate(self.charts[2:]):
            grid.addWidget(chart, index // 2, index % 2)
        graphs_layout.addLayout(grid)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        body = QWidget()
        body_layout = QVBoxLayout(body)
        body_layout.addWidget(self.rad_box)
        body_layout.addWidget(self.bme_box)
        body_layout.addWidget(graphs)
        scroll.setWidget(body)
        layout.addWidget(scroll, stretch=1)

        self.status_line = QLabel("")
        self.status_line.setWordWrap(True)
        layout.addWidget(self.status_line)

        self._show_radsens_rows(False)
        self._show_native_rows(False)

    def _build_writes(self) -> QWidget:
        box = QGroupBox("Register writes")
        layout = QVBoxLayout(box)
        self.arm_writes = QCheckBox("Arm register writes")
        self.arm_writes.toggled.connect(self._toggle_writes)
        layout.addWidget(self.arm_writes)
        note = QLabel(
            "Disabled until the warning is accepted. Each write confirms again. "
            "A short or out-of-range value is ignored by the firmware."
        )
        note.setWordWrap(True)
        layout.addWidget(note)

        self.native_write_box = QWidget()
        native_form = QFormLayout(self.native_write_box)
        self.window_spin = QSpinBox()
        self.window_spin.setRange(1, 400)
        self.window_spin.setSuffix(" s")
        self.holdoff_spin = QSpinBox()
        self.holdoff_spin.setRange(0, 300)
        self.holdoff_spin.setSuffix(" µs")
        self.cal_edit = QLineEdit()
        self.cal_edit.setPlaceholderText("Q16.16, e.g. 604948")
        native_form.addRow("Window", self._write_row(self.window_spin, self._write_window))
        native_form.addRow("Holdoff", self._write_row(self.holdoff_spin, self._write_holdoff))
        native_form.addRow("Calibration", self._write_row(self.cal_edit, self._write_calibration))
        layout.addWidget(self.native_write_box)

        self.radsens_write_box = QWidget()
        rad_form = QFormLayout(self.radsens_write_box)
        self.address_spin = QSpinBox()
        self.address_spin.setRange(0x03, 0x77)
        self.address_spin.setDisplayIntegerBase(16)
        self.address_spin.setPrefix("0x")
        self.sensitivity_spin = QSpinBox()
        self.sensitivity_spin.setRange(0, 510)
        self.lp_combo = QComboBox()
        self.lp_combo.addItem("0 off", 0)
        self.lp_combo.addItem("1 on", 1)
        self.led_combo = QComboBox()
        self.led_combo.addItem("0 off", 0)
        self.led_combo.addItem("1 on", 1)
        self.hv_combo = QComboBox()
        self.hv_combo.addItem("0 off", 0)
        self.hv_combo.addItem("1 on", 1)
        rad_form.addRow("Address", self._write_row(self.address_spin, self._write_address))
        rad_form.addRow("Sensitivity", self._write_row(self.sensitivity_spin, self._write_sensitivity))
        rad_form.addRow("Low power", self._write_row(self.lp_combo, self._write_lp))
        rad_form.addRow("LED", self._write_row(self.led_combo, self._write_led))
        rad_form.addRow("HV generator", self._write_row(self.hv_combo, self._write_hv))
        layout.addWidget(self.radsens_write_box)

        self.native_write_box.hide()
        self.radsens_write_box.hide()
        self._apply_write_enabled()
        return box

    def _write_row(self, editor: QWidget, slot) -> QWidget:
        row = QWidget()
        layout = QHBoxLayout(row)
        layout.setContentsMargins(0, 0, 0, 0)
        button = QPushButton("Write")
        button.clicked.connect(slot)
        if not hasattr(self, "write_buttons"):
            self.write_buttons = []
        self.write_buttons.append(button)
        layout.addWidget(editor, stretch=1)
        layout.addWidget(button)
        return row

    def _show_keys(self, keys: tuple[str, ...], show: bool) -> None:
        for key in keys:
            self._set_row_visible(self.field_form[key], self.rad_labels[key], show)

    def _show_native_rows(self, show: bool) -> None:
        self._show_keys(
            ("dose", "cpm", "status", "sequence", "raw", "elapsed", "min_gap",
             "calibration", "window", "holdoff"),
            show,
        )
        self.native_write_box.setVisible(show and self.native_writable)
        if not show:
            self.histogram.hide()

    def _show_radsens_rows(self, show: bool) -> None:
        self._show_keys(
            ("dynamic", "static", "hv_status", "firmware", "address",
             "sensitivity", "led", "lp", "hv_generator"),
            show,
        )
        self.radsens_write_box.setVisible(show)

    @staticmethod
    def _set_row_visible(form: QFormLayout, field: QLabel, show: bool) -> None:
        field.setVisible(show)
        label = form.labelForField(field)
        if label is not None:
            label.setVisible(show)

    def toggle_connect(self) -> None:
        if self.worker is not None:
            self.disconnect_bridge()
            return
        port = self.port_edit.text().strip()
        if not port:
            QMessageBox.warning(self, "Port", "Set a serial port path.")
            return
        self.worker = PollWorker(port, self.write_queue)
        self.worker.snapshot.connect(self.on_snapshot)
        self.worker.failed.connect(self.on_failed)
        self.worker.start()
        self.connect_btn.setText("Disconnect")
        self.port_edit.setEnabled(False)
        self.mode_label.setText(f"Connecting to {port}…")
        self.status_line.setText("")
        self._apply_write_enabled()

    def disconnect_bridge(self) -> None:
        if self.worker is not None:
            self.worker.stop()
            self.worker.wait(3000)
            self.worker = None
        self.connect_btn.setText("Connect")
        self.port_edit.setEnabled(True)
        self.mode_label.setText("Disconnected")
        self._apply_write_enabled()

    def _history_caption(self) -> str:
        return (
            f"{self.history.sample_count()} / {self.history.maxlen} samples, "
            "kept in memory for this window"
        )

    def _on_chart_hover(self, source: HistoryChart, t: Optional[float]) -> None:
        for chart in self.charts:
            chart.cursor_t = t
            chart.show_readout = chart is source and t is not None
            chart.update()

    def clear_history(self) -> None:
        self.history.clear()
        self.history_label.setText(self._history_caption())
        for chart in self.charts:
            chart.update()

    def _apply_write_enabled(self) -> None:
        armed = self.writes_armed and self.worker is not None
        for button in getattr(self, "write_buttons", []):
            button.setEnabled(armed)

    def _toggle_writes(self, checked: bool) -> None:
        if checked:
            answer = QMessageBox.warning(
                self,
                "Arm register writes",
                "Writes change the live device.\n\n"
                "Native v2 stores calibration (0x12, non-zero Q16.16), the static "
                "window (0x1E, 1..400 s), and the holdoff (0x20, 0..300 µs) in "
                "EEPROM. A calibration write is not a new accepted tube coefficient.\n\n"
                "RadSens can store a new address and sensitivity. Address 0x76 "
                "collides with the BME680. The low-power bit and the LED do not "
                "change the voltage or the count.\n\n"
                "RadSens 0x11 starts or stops the HV generator. Writing 1 brings up "
                "380 V. Writing 0 turns it off. The setpoint stays 380 V at 2400 Hz. "
                "Stay clear of the HV section.\n\n"
                "Each write still asks for confirmation.",
                QMessageBox.StandardButton.Ok | QMessageBox.StandardButton.Cancel,
                QMessageBox.StandardButton.Cancel,
            )
            if answer != QMessageBox.StandardButton.Ok:
                self.arm_writes.blockSignals(True)
                self.arm_writes.setChecked(False)
                self.arm_writes.blockSignals(False)
                checked = False
        self.writes_armed = checked
        self._apply_write_enabled()

    def _confirm_write(self, text: str) -> bool:
        answer = QMessageBox.warning(
            self,
            "Write register",
            text,
            QMessageBox.StandardButton.Ok | QMessageBox.StandardButton.Cancel,
            QMessageBox.StandardButton.Cancel,
        )
        return answer == QMessageBox.StandardButton.Ok

    def _enqueue(
        self,
        i2c_addr: int,
        payload: bytes,
        summary: str,
        new_radsens_addr: Optional[int] = None,
    ) -> None:
        if self.worker is None:
            self.status_line.setText("Connect before writing")
            return
        self.write_queue.put(
            RegisterWrite(i2c_addr, payload, summary, new_radsens_addr)
        )
        self._notice = "Queued " + summary
        self.status_line.setText(self._notice)

    def _set_spin(self, spin: QSpinBox, value: int) -> None:
        if spin.hasFocus():
            return
        spin.blockSignals(True)
        spin.setValue(value)
        spin.blockSignals(False)

    def _set_combo(self, combo: QComboBox, value: int) -> None:
        if combo.hasFocus():
            return
        index = combo.findData(value)
        if index >= 0:
            combo.setCurrentIndex(index)

    def _write_window(self) -> None:
        seconds = self.window_spin.value()
        if not self._confirm_write(
            f"Write {seconds} s to native 0x1E. Dose and CPM use this window on the "
            "next snapshot. The value is stored in EEPROM."
        ):
            return
        self._enqueue(NATIVE_ADDR, encode_native_window(seconds), f"window {seconds} s")

    def _write_holdoff(self) -> None:
        microseconds = self.holdoff_spin.value()
        if not self._confirm_write(
            f"Write {microseconds} µs to native 0x20. The next accepted edge holds "
            "off further edges for this long. The value is stored in EEPROM. "
            "Zero counts every falling edge."
        ):
            return
        self._enqueue(
            NATIVE_ADDR, encode_native_holdoff(microseconds), f"holdoff {microseconds} µs"
        )

    def _write_calibration(self) -> None:
        text = self.cal_edit.text().strip()
        try:
            q16 = int(text, 16) if text.lower().startswith("0x") else int(text, 10)
            payload = encode_native_calibration(q16)
        except ValueError as exc:
            QMessageBox.warning(self, "Calibration", str(exc))
            return
        if not self._confirm_write(
            f"Write Q16.16 {q16} ({q16 / 65536.0:.5f} nSv/h per CPM) to 0x12. "
            "This replaces the live coefficient and is stored in EEPROM. "
            "It is not a new accepted sensitivity. Zero is rejected."
        ):
            return
        self._enqueue(NATIVE_ADDR, payload, f"calibration {q16}")

    def _radsens_addr_now(self) -> int:
        if self.last_radsens is not None:
            return self.last_radsens.i2c_address
        return RADSENS_ADDR

    def _write_address(self) -> None:
        address = self.address_spin.value()
        collide = ""
        if address == BME_ADDR:
            collide = " 0x76 is the BME680 on this board, so the next transaction collides."
        if not self._confirm_write(
            f"Write RadSens address 0x{address:02X}.{collide} "
            "The next transaction uses it, and it is stored in EEPROM."
        ):
            return
        self._enqueue(
            self._radsens_addr_now(),
            encode_radsens_address(address),
            f"address 0x{address:02X}",
            address,
        )

    def _write_sensitivity(self) -> None:
        value = self.sensitivity_spin.value()
        if not self._confirm_write(
            f"Write sensitivity {value} impulses/µR to RadSens 0x12, little-endian. "
            "It is stored in EEPROM. This does not change the native calibration."
        ):
            return
        self._enqueue(
            self._radsens_addr_now(),
            encode_radsens_sensitivity(value),
            f"sensitivity {value}",
        )

    def _write_lp(self) -> None:
        value = int(self.lp_combo.currentData())
        if not self._confirm_write(
            f"Write {value} to RadSens 0x0C. The bit is stored and does not change "
            "voltage, frequency, or counting."
        ):
            return
        self._enqueue(
            self._radsens_addr_now(),
            encode_radsens_byte(0x0C, value),
            f"low power {value}",
        )

    def _write_led(self) -> None:
        value = int(self.led_combo.currentData())
        if not self._confirm_write(
            f"Write {value} to RadSens 0x14. In run, 1 flashes the LED on each pulse "
            "and 0 leaves it off."
        ):
            return
        self._enqueue(
            self._radsens_addr_now(),
            encode_radsens_byte(0x14, value),
            f"LED {value}",
        )

    def _write_hv(self) -> None:
        value = int(self.hv_combo.currentData())
        if value == 1:
            text = (
                "Write 1 to RadSens 0x11. This starts the HV generator. After it "
                "settles, 380 V is present. The setpoint stays 380 V at 2400 Hz. "
                "Stay clear of the HV section."
            )
        else:
            text = (
                "Write 0 to RadSens 0x11. This stops the HV generator. "
                "The setpoint stays 380 V at 2400 Hz."
            )
        if not self._confirm_write(text):
            return
        self._enqueue(
            self._radsens_addr_now(),
            encode_radsens_byte(0x11, value),
            f"HV generator {value}",
        )

    def export_csv(self) -> None:
        if self.history.sample_count() == 0 and not (
            self.last_native is not None and self.last_native.has_histogram
        ):
            QMessageBox.information(self, "Export CSV", "No samples to export yet.")
            return
        path, _ = QFileDialog.getSaveFileName(
            self, "Export CSV", "geigernode.csv", "CSV (*.csv)"
        )
        if not path:
            return
        text = render_csv(self.history, self.last_native)
        with open(path, "w", encoding="utf-8", newline="") as handle:
            handle.write(text)
        self._notice = f"Wrote {path}"
        self.status_line.setText(self._notice)

    def on_failed(self, message: str) -> None:
        self.status_line.setText(message)
        self.disconnect_bridge()

    def on_snapshot(self, snap: Snapshot) -> None:
        scanned = ", ".join(f"0x{a:02X}" for a in snap.scanned) or "none"
        if snap.mode == "native":
            version = snap.native.protocol if snap.native is not None else 0
            self.native_writable = version >= 2
            self.mode_label.setText(f"Native v{version} @ 0x67  |  scan: {scanned}")
            self._show_native_rows(True)
            self._show_radsens_rows(False)
            self.rad_labels["protocol"].setText(f"GeigerNode native v{version}")
        elif snap.mode == "radsens":
            address = snap.radsens.i2c_address if snap.radsens is not None else RADSENS_ADDR
            self.mode_label.setText(f"RadSens @ 0x{address:02X}  |  scan: {scanned}")
            self._show_native_rows(False)
            self._show_radsens_rows(True)
            self.rad_labels["protocol"].setText("ClimateGuard RadSens")
        else:
            self.mode_label.setText(f"No GeigerNode  |  scan: {scanned}")
            self._show_native_rows(False)
            self._show_radsens_rows(False)
            self.rad_labels["protocol"].setText("absent")

        if snap.native is not None:
            n = snap.native
            self.rad_labels["dose"].setText(
                f"{n.dose_nsvh} nSv/h  ({n.dose_usvh:.3f} µSv/h)"
            )
            self.rad_labels["cpm"].setText(f"{n.cpm:.1f} CPM  (×10 = {n.cpm_x10})")
            bits = ", ".join(n.status_names()) or "none"
            self.rad_labels["status"].setText(
                f"{n.hv_attempts:02X} {n.status_lo:02X}  "
                f"attempts={n.hv_attempts}  [{bits or 'none'}]"
            )
            self.rad_labels["sequence"].setText(str(n.sequence))
            self.rad_labels["raw"].setText(str(n.count))
            self.rad_labels["elapsed"].setText(f"{n.elapsed_ms} ms")
            if n.protocol >= 2:
                self.rad_labels["window"].setText(f"{n.window_s} s")
                self.rad_labels["holdoff"].setText(f"{n.holdoff_us} µs")
            else:
                self.rad_labels["window"].setText(dash())
                self.rad_labels["holdoff"].setText(dash())
            self.rad_labels["calibration"].setText(
                f"{n.cal_nsvh_per_cpm:.5f} nSv/h per CPM  (Q16.16 0x{n.cal_q16:08X})"
            )
            if n.protocol >= 2:
                self._set_spin(self.window_spin, n.window_s or 1)
                self._set_spin(self.holdoff_spin, n.holdoff_us)
                if not self.cal_edit.hasFocus():
                    self.cal_edit.setText(str(n.cal_q16))
            if n.has_histogram:
                if n.min_interval_us is None or n.min_interval_us == 0xFFFFFFFF:
                    self.rad_labels["min_gap"].setText("none yet")
                else:
                    self.rad_labels["min_gap"].setText(f"{n.min_interval_us} µs")
                self.histogram.set_bins(
                    n.histogram_bins, n.min_interval_us, n.histogram_saturated
                )
                self.histogram.show()
            else:
                self.rad_labels["min_gap"].setText(dash())
                self.histogram.hide()
            self.last_native = n

        if snap.radsens is not None:
            r = snap.radsens
            self.rad_labels["dynamic"].setText(
                f"{r.dynamic_urh:.1f} µR/h  ({r.dynamic_usvh:.3f} µSv/h)"
            )
            self.rad_labels["static"].setText(
                f"{r.static_urh:.1f} µR/h  ({r.static_usvh:.3f} µSv/h)"
            )
            self.rad_labels["hv_status"].setText(
                f"{r.hv_status_name()}  attempts={r.hv_attempts}"
            )
            self.rad_labels["hv_generator"].setText("on" if r.hv_generator else "off")
            self.rad_labels["address"].setText(f"0x{r.i2c_address:02X}")
            self.rad_labels["sensitivity"].setText(f"{r.sensitivity} impulses/µR")
            self.rad_labels["led"].setText("on" if r.led_enable else "off")
            self.rad_labels["lp"].setText("on" if r.lp_mode else "off")
            self.rad_labels["firmware"].setText(f"0x{r.firmware:02X}")
            self._set_spin(self.address_spin, r.i2c_address)
            self._set_spin(self.sensitivity_spin, r.sensitivity)
            self._set_combo(self.lp_combo, 1 if r.lp_mode else 0)
            self._set_combo(self.led_combo, 1 if r.led_enable else 0)
            self._set_combo(self.hv_combo, 1 if r.hv_generator else 0)
            self.last_radsens = r

        if snap.bme is not None:
            b = snap.bme
            self.bme_labels["temperature"].setText(f"{b.temperature_c:.2f} °C")
            self.bme_labels["pressure"].setText(f"{b.pressure_hpa:.2f} hPa")
            self.bme_labels["humidity"].setText(f"{b.humidity_rh:.2f} %RH")
            gas = f"{b.gas_ohms:.0f} Ω" if b.gas_valid else "invalid"
            self.bme_labels["gas"].setText(gas)
            flags = []
            if b.new_data:
                flags.append("NEW_DATA")
            if b.gas_valid:
                flags.append("GAS_VALID")
            if b.heat_stable:
                flags.append("HEAT_STABLE")
            self.bme_labels["flags"].setText(", ".join(flags) or "none")
        elif snap.bme_present:
            for lab in self.bme_labels.values():
                lab.setText("waiting…")
        else:
            for lab in self.bme_labels.values():
                lab.setText("absent")

        if snap.native is not None or snap.radsens is not None or snap.bme is not None:
            record_snapshot(self.history, snap)
            self.history_label.setText(self._history_caption())
            for chart in self.charts:
                chart.update()

        if snap.notice:
            self._notice = snap.notice
        self.status_line.setText(snap.error or self._notice)

    def closeEvent(self, event) -> None:  # noqa: N802
        self.disconnect_bridge()
        super().closeEvent(event)


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--port",
        default=default_port() or "",
        help="ESP32-C3 bridge serial port (default: Espressif USB-JTAG by-id)",
    )
    args = parser.parse_args(argv)
    app = QApplication(sys.argv)
    win = MainWindow(args.port)
    win.show()
    if args.port:
        win.toggle_connect()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
