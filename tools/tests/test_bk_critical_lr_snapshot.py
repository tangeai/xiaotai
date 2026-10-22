#!/usr/bin/env python3
"""Exercise the build-only CP/AP symbol contract through its CLI."""

import hashlib
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "platforms/beken/tools/instrument_critical_lr.py"
NM_LINES = {
    "debug_event_fault": 0x28009200,
    "debug_queue_wait": 0x28009300,
    "debug_port_wait": 0x28009000,
    "debug_event_wait": 0x28009100,
    "port_uxCriticalNestingCheckBackup": 0x28008220,
    "xKernelLock": 0x28008154,
    "xTickCount": 0x280186BC,
    "pxCurrentTCBs": 0x28018538,
    "port_uxCriticalNesting": 0x28018908,
    "port_uxEnterCriticalLrStackCount": 0x280082D8,
    "port_uxEnterCriticalLrStack": 0x280082E0,
    "port_uxEnterCriticalLrRecordCount": 0x28008230,
    "port_uxEnterCriticalLrRecord": 0x28008238,
}
SOURCE = (b"static int s_mb_ipc_work_state;\r\n"
          b"void mb_ipc_heartbeat_notify(u32 cpu_id);\r\n"
          b"void timeout_path(void) {\r\n"
          b'\t\t\t\tBK_DUMP_OUT("[DEBUG-bk-hb] ap-hw run=%u", 0);\r\n'
          b"}\r\n")


class CriticalLrSnapshotTests(unittest.TestCase):
    def test_apply_verify_restore_and_reject_stale_ap_elf(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            sdk = directory / "mb_ipc_heartbeat.c"
            nm = directory / "app.nm"
            backup = directory / "before.c"
            contract = directory / "symbols.txt"
            sdk.write_bytes(SOURCE)
            nm.write_text("".join(
                f"{address:08x} b {name}\n" for name, address in NM_LINES.items()
            ))
            base = [sys.executable, str(SCRIPT), "--sdk-file", str(sdk),
                    "--nm-file", str(nm), "--backup", str(backup),
                    "--contract", str(contract), "--expected-source-sha256",
                    hashlib.sha256(SOURCE).hexdigest()]

            subprocess.run(base[:2] + ["apply"] + base[2:], check=True)
            updated = sdk.read_bytes()
            self.assertIn(b"ap-lr core=%u stack=%u pc=0x%08x", updated)
            self.assertIn(b"ap-lr core=%u recent=%u pc=0x%08x", updated)
            self.assertIn(b"pm-lv=%u pm-mb=%u", updated)
            self.assertIn(b"ap-port core=%u phase=%u mux=0x%08x", updated)
            self.assertIn(b"ap-event core=%u phase=%u list=0x%08x", updated)
            self.assertIn(b"ap-first-reentry core=%u offset=%u", updated)
            self.assertIn(b"ap-queue-boundary core=%u offset=%u", updated)
            self.assertIn(b"ap-event-tail core=%u", updated)
            self.assertIn(b"if (depth > AP_LR_MAX_DEPTH)", updated)
            self.assertEqual(updated.count(b"debug_ap_critical_lr_snapshot(0)"), 1)
            subprocess.run(base[:2] + ["verify"] + base[2:], check=True)

            nm.write_text(nm.read_text().replace("280082e0", "280082e4"))
            stale = subprocess.run(base[:2] + ["verify"] + base[2:],
                                   capture_output=True, text=True)
            self.assertNotEqual(stale.returncode, 0)
            self.assertIn("address changed", stale.stderr)

            subprocess.run(base[:2] + ["restore"] + base[2:], check=True)
            self.assertEqual(sdk.read_bytes(), SOURCE)

    def test_reject_missing_lr_symbols_before_mutation(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            sdk = directory / "mb_ipc_heartbeat.c"
            nm = directory / "app.nm"
            sdk.write_bytes(SOURCE)
            nm.write_text("28008154 d xKernelLock\n")
            result = subprocess.run(
                [sys.executable, str(SCRIPT), "apply", "--sdk-file", str(sdk),
                 "--nm-file", str(nm), "--backup", str(directory / "before.c"),
                 "--contract", str(directory / "symbols.txt"),
                 "--expected-source-sha256", hashlib.sha256(SOURCE).hexdigest()],
                capture_output=True, text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("missing critical LR symbols", result.stderr)
            self.assertEqual(sdk.read_bytes(), SOURCE)


if __name__ == "__main__":
    unittest.main()
