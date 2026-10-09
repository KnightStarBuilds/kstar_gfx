#!/usr/bin/env python3
"""Regression: localized shadow errors must not hide in background averages."""
from pathlib import Path
import runpy
import unittest


compare = runpy.run_path(str(Path(__file__).resolve().parents[1] /
                            "scripts/run_gnm_demo.py"))["compare_images"]


class CaptureComparisonTests(unittest.TestCase):
    def setUp(self):
        self.width, self.height = 320, 180
        self.reference = (self.width, self.height,
                          bytes([64]) * (self.width * self.height * 3))

    def test_localized_shadow_error_is_rejected(self):
        pixels = bytearray(self.reference[2])
        # A small solid patch models the observed spurious shadow band. It is
        # diluted below the global/coarse limits but must fail local comparison.
        for y in range(80, 88):
            for x in range(162, 178):
                offset = (y * self.width + x) * 3
                pixels[offset:offset + 3] = bytes([220, 220, 220])
        result = compare((self.width, self.height, pixels), self.reference)
        self.assertLess(result["mean_absolute_error"], result["limits"]["mean"])
        self.assertLess(result["worst_4x4_tile_mean_error"],
                        result["limits"]["worst_4x4_tile_mean"])
        self.assertEqual(result["status"], "failed")

    def test_one_lsb_conversion_difference_is_accepted(self):
        pixels = bytes([65]) * len(self.reference[2])
        result = compare((self.width, self.height, pixels), self.reference)
        self.assertEqual(result["status"], "passed")


if __name__ == "__main__":
    unittest.main()
