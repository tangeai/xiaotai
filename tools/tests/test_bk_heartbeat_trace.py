"""Classify the last AP stage before a BK IPC heartbeat reset."""

import sys
import unittest
from pathlib import Path


TOOLS = (Path(__file__).resolve().parents[2] /
         'firmware/beken/bk7258/lckfb-bk7258-xiaotai/tools')
sys.path.insert(0, str(TOOLS))

from analyze_runtime_log import analyze  # noqa: E402


class BKHeartbeatTraceTest(unittest.TestCase):
    def test_heartbeat_timeout_reports_last_stage_of_each_boot(self):
        result = analyze([
            'ap0:xiaotai_:I(120):AP boot reached application entry',
            'ap0:xiaotai_:I(2300):HBTRACE tirtc-start-enter',
            'hrt:E(10335):IPC[1]heartbeat timeout 2334,10335',
            'ap0:xiaotai_:I(214):AP boot reached application entry',
            'ap0:xiaotai_:I(2398):HBTRACE tirtc-start-exit rc=0',
            'ap0:xiaotai_:I(10690):HBTRACE metrics-task-scan-enter',
            'hrt:E(18343):IPC[1]heartbeat timeout 10343,18343',
        ]).result(None)
        self.assertEqual('FAIL', result['status'])
        self.assertEqual([
            {'line': 3, 'last_stage': 'tirtc-start-enter'},
            {'line': 7, 'last_stage': 'metrics-task-scan-enter'},
        ], result['heartbeat_timeouts'])

    def test_completed_stage_is_not_reported_as_stalled(self):
        result = analyze([
            'HBTRACE metrics-task-scan-enter',
            'HBTRACE metrics-task-scan-exit rc=0',
            'hrt:E(18000):IPC[1]heartbeat timeout 10000,18000',
        ]).result(None)
        self.assertEqual('metrics-task-scan-exit rc=0',
                         result['heartbeat_timeouts'][0]['last_stage'])


if __name__ == '__main__':
    unittest.main()
