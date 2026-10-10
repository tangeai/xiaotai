"""Current serial logs retain boundary evidence without inventing peer delivery."""
import importlib.util
import unittest
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / 'analyze_call_link.py'


class CallLinkAnalyzer(unittest.TestCase):
    def analyzer(self):
        spec = importlib.util.spec_from_file_location('call_link', PATH)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module.summarize

    def test_link_deltas_and_media_gaps_are_independent_observations(self):
        result = self.analyzer()('''
I (100) starter_media: media session started mode=4 generation=1 video=h264
I (1000) stats: [DEBUG-link71] host rx-valid/stack-ok/stack-fail=100/90/1 tx-queued/drop/bus-written=100/2/80 throttle=0
I (6000) p4_video: downlink generation=1 mode=4 tx=25 tx-fail=0 tx-ret=0 send-buffer=100000/2097152 subscribed=1 rx=10
I (11000) stats: [DEBUG-link71] host rx-valid/stack-ok/stack-fail=130/115/3 tx-queued/drop/bus-written=160/2/100 throttle=1
I (11001) camera_pipeline: CAM 640x480@5 f=5.0 br=273k gap=199/203ms
I (11002) call_video: VRX 640x480 in=2.4/307k q/d/c/o=2.4/2.4/2.5/2.5
I (11003) call_video: video stall: stage=input decode=108162us input_gap=694535us done_gap=683805us
I (12000) p4_video: downlink generation=1 mode=4 tx=50 tx-fail=0 tx-ret=0 send-buffer=200000/2097152 subscribed=1 rx=11
Audio RX buffer: slots=8 overflow=18 prefill-waits=0
secret-token=DO_NOT_EXPORT
''')
        call = result['calls'][0]
        self.assertEqual(call['host_intervals'][0]['delta']['rx-valid'], 30)
        self.assertEqual(call['host_intervals'][0]['delta']['stack-fail'], 2)
        self.assertEqual(call['video_intervals'][0]['rx_fps'], 1/6)
        self.assertEqual(call['video_intervals'][0]['send_buffer_delta_bytes'], 100000)
        self.assertEqual(call['camera'][0]['fps'], 5.0)
        self.assertEqual(call['renderer_input'][0]['fps'], 2.4)
        self.assertEqual(call['input_gaps'][0]['input_gap_ms'], 694.535)
        self.assertEqual(call['audio_overflow_observations'], [18])
        self.assertNotIn('DO_NOT_EXPORT', str(result))

    def test_calls_reboots_and_counter_resets_never_create_negative_rates(self):
        parse = self.analyzer()
        result = parse('''
I (9000) starter_media: media session started mode=4 generation=7 video=h264
I (9100) stats: [DEBUG-link71] host rx-valid/stack-ok/stack-fail=100/90/1 tx-queued/drop/bus-written=100/2/80 throttle=0
I (10000) stats: [DEBUG-link71] host rx-valid/stack-ok/stack-fail=1/1/0 tx-queued/drop/bus-written=1/0/1 throttle=0
I (11000) starter_runtime: runtime state 6 -> 0 session=7 connection=0
I (12000) camera_pipeline: CAM 640x480@5 f=1.0 br=10k gap=1/2ms
I (100) starter_media: media session started mode=3 generation=1 video=h264
I (200) p4_video: downlink generation=99 mode=3 tx=1 tx-fail=0 tx-ret=0 send-buffer=0/2097152 subscribed=1 rx=1
''')
        self.assertEqual(len(result['calls']), 2)
        self.assertEqual(result['calls'][0]['host_intervals'], [])
        self.assertEqual(result['calls'][0]['counter_resets'], 1)
        self.assertEqual(result['calls'][0]['camera'], [])
        self.assertEqual(result['calls'][1]['video_samples'], [])
        self.assertEqual(parse('no statistics')['calls'], [])


if __name__ == '__main__':
    unittest.main()
