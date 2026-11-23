"""Both platform registries and both handbooks must expose portable service commands."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ESP = ROOT / 'platforms/esp-idf/components/starter_console/src/starter_console.c'
BK = ROOT / 'platforms/beken/runtime/src/xiaotai_console.c'
PORTABLE = set('help status version restart wifi-change wifi-set wifi-clear tirtc-set tirtc-clear log-level ai-start ai-stop device-call device-answer wechat-call wechat-answer call-answer call-hangup call-status room-status room-sync room-join room-leave room-talk-start room-talk-stop ep-show ep-set ep-reset ntp-show ntp-set ntp-reset'.split())


class SerialCommandParity(unittest.TestCase):
    def test_platform_registries_and_manuals(self):
        esp = set(re.findall(r'register_command\(\s*"([a-z-]+)"', ESP.read_text())) | {'help'}
        bk = set(re.findall(r'\{"([a-z-]+)",', BK.read_text())) | {'help'}
        self.assertFalse(PORTABLE - esp, sorted(PORTABLE - esp))
        self.assertFalse(PORTABLE - bk, sorted(PORTABLE - bk))
        for suffix in ('md', 'en.md'):
            text = (ROOT / f'docs/development/SERIAL_COMMANDS.{suffix}').read_text()
            commands = set(re.findall(r'`([a-z][a-z-]+)(?=[ `])', text))
            self.assertFalse((esp | bk) - commands, sorted((esp | bk) - commands))


if __name__ == '__main__':
    unittest.main()
