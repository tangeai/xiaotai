"""Periodic BK telemetry must not hold the SMP scheduler lock."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
METRICS = ROOT / "platforms/beken/runtime/src/xiaotai_metrics.c"


class TestBkMetricsNoKernelScan(unittest.TestCase):
    def test_periodic_snapshot_avoids_global_kernel_scan(self):
        source = METRICS.read_text()
        self.assertFalse("uxTaskGetSystemState(" in source,
                         "periodic metrics still call the SMP-wide locked task scan")
        self.assertTrue("uxTaskGetNumberOfTasks()" in source)


if __name__ == "__main__":
    unittest.main()
