"""Exercise the experiment gate via its CLI, including stale IDF configs."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / 'tools/check_cores3_cache_experiment.py'
BASE = '''CONFIG_IDF_TARGET="esp32s3"
CONFIG_SPIRAM_MODE_QUAD=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_ESP32S3_DATA_CACHE_32KB=y
CONFIG_ESP32S3_DATA_CACHE_SIZE=0x8000
CONFIG_ESP32S3_DATA_CACHE_LINE_32B=y
# CONFIG_ESP32S3_DATA_CACHE_LINE_64B is not set
CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE=32
CONFIG_COMPILER_OPTIMIZATION_DEBUG=y
CONFIG_STARTER_AI_UPLINK_G711A=y
'''
CANDIDATE = BASE.replace('CONFIG_ESP32S3_DATA_CACHE_LINE_32B=y',
                        '# CONFIG_ESP32S3_DATA_CACHE_LINE_32B is not set').replace(
    '# CONFIG_ESP32S3_DATA_CACHE_LINE_64B is not set', 'CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y').replace(
    'CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE=32', 'CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE=64')


class CacheExperiment(unittest.TestCase):
    def run_gate(self, candidate, baseline=BASE):
        with tempfile.TemporaryDirectory() as tmp:
            a, b = Path(tmp)/'a', Path(tmp)/'b'
            a.write_text(baseline); b.write_text(candidate)
            return subprocess.run([sys.executable, str(TOOL), '--baseline', str(a),
                                   '--candidate', str(b)], capture_output=True, text=True)

    def test_accepts_only_line_change_and_ignores_comments(self):
        result = self.run_gate(CANDIDATE+'# generated on another date\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('32 B -> 64 B', result.stdout)

    def test_rejects_stale_retained_sdkconfig(self):
        result = self.run_gate(BASE)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('candidate cache line', result.stderr)

    def test_rejects_additional_variables_even_unknown_ones(self):
        for bad in [CANDIDATE.replace('SIZE=0x8000', 'SIZE=0x10000'),
                    CANDIDATE.replace('DEBUG=y', 'DEBUG=n'),
                    CANDIDATE.replace('UPLINK_G711A=y', 'UPLINK_G711A=n'),
                    CANDIDATE+'CONFIG_NEW_AUDIO_TUNING=y\n',
                    CANDIDATE.replace('CONFIG_SPIRAM_SPEED_80M=y\n', '')]:
            with self.subTest(bad=bad):
                self.assertNotEqual(self.run_gate(bad).returncode, 0)

    def test_rejects_missing_inconsistent_and_wrong_board_configs(self):
        for bad in ['', CANDIDATE.replace('LINE_SIZE=64', 'LINE_SIZE=32'),
                    CANDIDATE.replace('esp32s3', 'esp32p4'),
                    CANDIDATE.replace('CONFIG_SPIRAM_MODE_QUAD=y', 'CONFIG_SPIRAM_MODE_OCT=y'),
                    CANDIDATE+'CONFIG_ESP32S3_DATA_CACHE_LINE_32B=y\n']:
            with self.subTest(bad=bad):
                self.assertNotEqual(self.run_gate(bad).returncode, 0)

    def test_current_board_defaults_select_candidate_without_ram_capacity_cost(self):
        sys.path.insert(0, str(ROOT/'tools'))
        from check_cores3_cache_experiment import read_config, require_cache
        values = read_config(ROOT/'firmware/esp-idf/esp32s3/m5stack-cores3-lite/sdkconfig.defaults')
        require_cache(values, 64, 'candidate defaults', resolved=False)


if __name__ == '__main__':
    unittest.main()
