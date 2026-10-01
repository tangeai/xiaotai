#!/usr/bin/env python3
import unittest

from analyze_runtime_log import analyze


class AnalyzeRuntimeLogTest(unittest.TestCase):
    def test_task_return_warning_is_not_a_cpu_crash(self):
        result = analyze([
            'I:METRICS {"state":"READY","internal":{"free":100},'
            '"psram":{"free":1000},"tasks":22}',
            "I:AI talk active generation=3",
            "I:audio stopped up=50 down=40 dropped=0 aec=60 ref-under=3 ref-over=0",
            "I:AI session finished generation=3 error=0",
            "OS:E:==> Task exits abnormally!",
            'I:METRICS {"state":"READY","internal":{"free":99},'
            '"psram":{"free":990},"tasks":22}',
        ]).result(None)
        self.assertEqual("PASS", result["status"])
        self.assertEqual(1, result["lifecycle"]["task_return_warnings"])
        self.assertEqual(-1, result["ready_resource_delta"]["internal_free"])
        self.assertEqual(5.0, result["media"]["ref_under_percent"])

    def test_cpu_fault_and_nonzero_session_error_fail(self):
        result = analyze([
            "AP crash happend, rr: 0x14",
            "UsageFault",
            "W:remote STREAM disconnected generation=2 error=-40008",
        ]).result(None)
        self.assertEqual("FAIL", result["status"])
        self.assertEqual(2, len(result["fatal_markers"]))
        self.assertEqual(-40008,
                         result["sessions"]["nonzero_errors"][0]["error"])

    def test_optional_aec_gate(self):
        lines = [
            "I:audio stopped up=10 down=10 dropped=0 aec=100 ref-under=25 ref-over=0"
        ]
        self.assertEqual("PASS", analyze(lines).result(None)["status"])
        self.assertEqual("FAIL", analyze(lines).result(20)["status"])

    def test_optional_drop_and_stack_gates(self):
        lines = [
            'I:METRICS {"state":"READY","internal":{"free":100},'
            '"psram":{"free":1000},"tasks":22,"stack_low_words":42}',
            "I:audio stopped up=100 down=100 dropped=81920 "
            "aec=100 ref-under=0 ref-over=0",
        ]
        result = analyze(lines).result(
            None, max_dropped_bytes=10240, min_stack_low_words=128
        )
        self.assertEqual("FAIL", result["status"])
        self.assertEqual(42, result["minimum_stack_low_words"])
        self.assertIn("audio dropped 81920 bytes exceeds 10240",
                      result["failures"])
        self.assertIn("minimum stack water 42 words is below 128",
                      result["failures"])


if __name__ == "__main__":
    unittest.main()
