import importlib.util
import unittest
from test_s3_product_regressions import ROOT

class AudioTraceAnalyzer(unittest.TestCase):
    def test_alignment_detects_missing_mutated_and_duplicate_packets(self):
        spec=importlib.util.spec_from_file_location('trace',ROOT/'tools/analyze_audio_trace.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        def dump(stages, lost=0):
            lines=[f'AUDIO_TRACE_ROW {i} {stage} 7 20 61 4 1 1 {fingerprint} 100 0'
                   for i,(stage,fingerprint) in enumerate(stages)]
            return '\n'.join(lines+[f'AUDIO_TRACE_END count={len(stages)} lost={lost}'])
        # Different tasks can record enqueue after dequeue; duplicates count.
        valid=[(1,42),(3,42),(2,42),(4,42)]*2
        self.assertTrue(module.analyze(dump(valid))['all_stages_match'])
        self.assertFalse(module.analyze(dump(valid[:-1]))['all_stages_match'])
        bad=valid[:];bad[-1]=(4,43)
        self.assertFalse(module.analyze(dump(bad))['all_stages_match'])
        for broken in [dump(valid,lost=1),dump(valid).replace('ROW 2 ','ROW 9 '),
                       dump(valid).split('AUDIO_TRACE_END')[0],dump([])]:
            with self.assertRaises(ValueError):module.analyze(broken)

if __name__=='__main__':unittest.main()
