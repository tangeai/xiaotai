"""Classify the last AP stage before a BK IPC heartbeat reset."""

import sys
import unittest
from pathlib import Path


TOOLS = (Path(__file__).resolve().parents[2] /
         'firmware/beken/bk7258/lckfb-bk7258-xiaotai/tools')
sys.path.insert(0, str(TOOLS))

from analyze_runtime_log import analyze  # noqa: E402


class BKHeartbeatTraceTest(unittest.TestCase):
    def test_heartbeat_timeout_keeps_two_ap_sram_samples(self):
        result = analyze([
            'AP boot reached application entry',
            '[DEBUG-bk-hb] ap-state sample=0 tick=1234 shell=0xf2eef2ee/0 '
            'mb=0x00000001/1 kernel=0xf2eef2ee/0 '
            'tcb0=0x28010100 tcb1=0x28010200',
            '[DEBUG-bk-hb] ap-state sample=1 tick=1254 shell=0xf2eef2ee/0 '
            'mb=0xf2eef2ee/0 kernel=0xf2eef2ee/0 '
            'tcb0=0x28010100 tcb1=0x28010200',
            'IPC[1]heartbeat timeout 1200,9200',
            'AP boot reached application entry',
            'IPC[1]heartbeat timeout 2000,10000',
        ]).result(None)
        self.assertEqual([
            {'sample': 0, 'tick': 1234, 'shell': {'owner': '0xf2eef2ee', 'count': 0},
             'mb': {'owner': '0x00000001', 'count': 1},
             'kernel': {'owner': '0xf2eef2ee', 'count': 0},
             'tcb0': '0x28010100', 'tcb1': '0x28010200'},
            {'sample': 1, 'tick': 1254, 'shell': {'owner': '0xf2eef2ee', 'count': 0},
             'mb': {'owner': '0xf2eef2ee', 'count': 0},
             'kernel': {'owner': '0xf2eef2ee', 'count': 0},
             'tcb0': '0x28010100', 'tcb1': '0x28010200'},
        ], result['heartbeat_timeouts'][0]['ap_state'])
        self.assertNotIn('ap_state', result['heartbeat_timeouts'][1])

    def test_heartbeat_timeout_keeps_ap_hardware_snapshot(self):
        result = analyze([
            'AP boot reached application entry',
            '[DEBUG-bk-hb] ap-hw run=0x00000404 cpu1=0x00000008 cpu2=0x00000000',
            '[DEBUG-bk-hb] rx-timeout count=16',
            'hrt:E(40239):IPC[1]heartbeat timeout 32239,40239',
            'AP boot reached application entry',
            'hrt:E(12000):IPC[1]heartbeat timeout 4000,12000',
        ]).result(None)
        self.assertEqual({
            'run': '0x00000404',
            'cpu1': '0x00000008',
            'cpu2': '0x00000000',
            'core1_halted': False,
            'core2_halted': True,
            'core1_powered_down': False,
            'core2_powered_down': True,
        }, result['heartbeat_timeouts'][0]['ap_hardware'])
        self.assertNotIn('ap_hardware', result['heartbeat_timeouts'][1])

    def test_rtos_assert_records_caller_and_context(self):
        result = analyze([
            '[DEBUG-bk-rtos] api=delay caller=0x28012345 irq=1 isr=0',
            '(1105566)Assert at: rtos_delay_milliseconds:1432',
            '[DEBUG-bk-rtos] api=mutex caller=0x28023456 irq=0 isr=1',
            '(10716)Assert at: rtos_lock_mutex:524',
        ]).result(None)
        self.assertEqual('FAIL', result['status'])
        self.assertEqual([
            {'line': 1, 'api': 'delay', 'caller': '0x28012345',
             'irq': 1, 'isr': 0},
            {'line': 3, 'api': 'mutex', 'caller': '0x28023456',
             'irq': 0, 'isr': 1},
        ], result['rtos_asserts'])

    def test_diagnostic_trace_identifies_pending_send_and_cp_receive_gap(self):
        result = analyze([
            'ap0:xiaotai_:I(100):AP boot reached application entry',
            'ap0:hrt:I(2100):[DEBUG-bk-hb] tx-enter seq=1 ms=2100',
            'ap0:hrt:I(2101):[DEBUG-bk-hb] tx-exit seq=1 rc=0 ms=2101',
            'ap0:hrt:I(4100):[DEBUG-bk-hb] tx-enter seq=2 ms=4100',
            'hrt:E(12000):[DEBUG-bk-hb] rx-timeout count=1',
            'hrt:E(12000):IPC[1]heartbeat timeout 4000,12000',
            'cp:E(12100):[DEBUG-bk-lwip-free] ptr=0x2801aec8 '
            'caller=0x2809abcd cp_heap=0x28083140',
        ]).result(None)
        self.assertEqual({
            'phase': 'tx-enter', 'seq': 2, 'ms': 4100,
        }, result['heartbeat_timeouts'][0]['heartbeat_tx'])
        self.assertEqual({
            'count': 1,
        }, result['heartbeat_timeouts'][0]['cp_rx'])
        self.assertEqual([{
            'line': 7, 'ptr': '0x2801aec8', 'caller': '0x2809abcd',
            'cp_heap': '0x28083140',
        }], result['invalid_frees'])

    def test_diagnostic_trace_resets_on_new_boot(self):
        result = analyze([
            '[DEBUG-bk-hb] tx-exit seq=3 rc=-1 ms=6100',
            'AP boot reached application entry',
            'IPC[1]heartbeat timeout 4000,12000',
        ]).result(None)
        self.assertNotIn('heartbeat_tx', result['heartbeat_timeouts'][0])

    def test_invalid_free_marker_alone_fails_stability_gate(self):
        result = analyze([
            '[DEBUG-bk-lwip-free] ptr=0x2801aec8 '
            'caller=0x2809abcd cp_heap=0x28083140',
        ]).result(None)
        self.assertEqual('FAIL', result['status'])
        self.assertIn('CP lwIP invalid free', result['failures'])

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
