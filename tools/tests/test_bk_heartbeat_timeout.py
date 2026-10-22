"""The CP heartbeat timeout matches the BK7258's known 8-second profile."""

from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / 'firmware/beken/bk7258/lckfb-bk7258-xiaotai'
SDK = Path(os.environ.get('SDK_DIR', ROOT / '.tools/bk_avdk_smp-release-v3.1.1.10'))
SOURCE = 'cp/middleware/driver/mailbox/mb_ipc_heartbeat.c'
PATCH = ROOT / 'platforms/beken/tools/patches/bk-avdk-heartbeat-8s.patch'


class BKHeartbeatTimeoutTest(unittest.TestCase):
    def test_cp_timeout_is_eight_seconds_when_watchdog_is_disabled(self):
        if not (SDK / SOURCE).is_file():
            self.skipTest('BK SDK checkout is required')
        build_script = (ROOT / "platforms/beken/tools/build.sh").read_text()
        self.assertIn(PATCH.name, build_script)
        pristine = subprocess.run(
            ['git', '-C', str(SDK), 'show', f'HEAD:{SOURCE}'],
            check=True, capture_output=True).stdout
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            sdk_file = temp / SOURCE
            sdk_file.parent.mkdir(parents=True)
            sdk_file.write_bytes(pristine)
            subprocess.run(['git', 'apply', '--ignore-whitespace', str(PATCH)], cwd=temp,
                           check=True, capture_output=True)
            source = sdk_file.read_text()
            begin = source.index('#define MB_IPC_HEARTBEAT_TIME')
            end = source.index('\n#endif', begin) + len('\n#endif')
            harness = temp / 'timeout.c'
            harness.write_text('#define CONFIG_WDT_EN 0\n'
                               '#define CONFIG_INT_WDT_PERIOD_MS 8000\n'
                               + source[begin:end] + '\n'
                               'int main(void) { return HB_TIMEOUT_MS == 8000 ? 0 : 1; }\n')
            executable = temp / 'timeout'
            subprocess.run(['cc', '-std=c11', str(harness), '-o', str(executable)],
                           check=True, capture_output=True)
            self.assertEqual(subprocess.run([str(executable)]).returncode, 0)


if __name__ == '__main__':
    unittest.main()
