"""Run with: python3 -m unittest discover -s test -p 'test_*.py'."""
import runpy
import sys
import types
import unittest
from pathlib import Path
from unittest.mock import patch

# Parsing tests need no serial hardware or installed pyserial.
with patch.dict(sys.modules, {"serial": types.ModuleType("serial")}):
    dashboard = runpy.run_path(str(Path(__file__).resolve().parents[1] / "src/dashboard.py"))
process = dashboard["_process_line"]


class DashboardTest(unittest.TestCase):
    def feed_packet(self, present, valid, sequence=42, include_mlx=True):
        ctx = dashboard["_new_packet_ctx"]()
        lines = [f"Packet {sequence}, RSSI -70.0 dBm, SNR 8.0 dB",
                 "Grid-EYE: " + ("valid" if valid & 1 else "unavailable")]
        if present & 1:
            lines += ["\t".join(["25.0" if valid & 1 else "ERR"] * 8)] * 8
        lines += ["PMSA003I: PM1=1 PM2.5=2 PM10=3 ug/m3" if valid & 2
                  else "PMSA003I: unavailable"]
        if include_mlx:
            lines += ["MLX90614: Object=-12.3 C, Ambient=23.4 C" if valid & 8
                      else "MLX90614: unavailable"]
        lines += ["Fire: NORMAL (reasons: none)",
                  "DHT11: 24.0 C, 55.0 % RH" if valid & 4 else "DHT11: unavailable"]
        for line in lines:
            ctx = process(line, ctx)
        return process.__globals__["latest"]

    def test_all_sensor_combinations_and_failures(self):
        for present in range(16):
            for valid in range(16):
                if valid & ~present:
                    continue
                with self.subTest(present=present, valid=valid):
                    data = self.feed_packet(present, valid)
                    for key, bit in [("gridEyeValid", 1), ("pmValid", 2),
                                     ("dhtValid", 4), ("mlxValid", 8)]:
                        self.assertEqual(data[key], bool(valid & bit))
                    self.assertEqual(len(data["gridEye"]), 64)
                    if valid & 8:
                        self.assertEqual(data["mlxObject"], -12.3)
                        self.assertEqual(data["mlxAmbient"], 23.4)

    def test_old_server_or_missing_sensor_does_not_reuse_reading(self):
        self.feed_packet(8, 8)
        data = self.feed_packet(0, 0, sequence=43, include_mlx=False)
        self.assertEqual(data["sequence"], 43)
        self.assertFalse(data["mlxValid"])
        self.assertEqual(data["mlxObject"], 0)


if __name__ == "__main__":
    unittest.main()
