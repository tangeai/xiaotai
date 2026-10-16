"""Check the built public IPC startup path keeps its worker stack in SRAM."""

import os
import re
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / 'firmware/beken/bk7258/lckfb-bk7258-xiaotai'
ELF = PROJECT / 'build/bk7258/lckfb-bk7258-xiaotai/bk7258_ap/app.elf'
OBJDUMP = ROOT / '.tools/gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-objdump'


class BKIPCBootStackTest(unittest.TestCase):
    def test_bk_ipc_init_worker_uses_sram(self):
        elf = Path(os.environ.get('BK_AP_ELF', ELF))
        objdump = Path(os.environ.get('BK_OBJDUMP', OBJDUMP))
        if not elf.is_file() or not objdump.is_file():
            self.skipTest('built BK AP ELF and objdump are required')
        result = subprocess.run(
            [str(objdump), '-d', '-j', '.text.bk_ipc_core_init', str(elf)],
            capture_output=True, text=True, check=True)
        self.assertRegex(result.stdout, r'<bk_ipc_core_init>:',
                         'IPC startup must remain a separately inspectable function')
        calls = re.findall(r'\bbl\s+[0-9a-f]+\s+<([^>]+)>', result.stdout)
        self.assertIn('rtos_create_sram_thread', calls,
                      'the IPC worker must start without allocating a PSRAM stack')
        self.assertNotIn('rtos_create_thread', calls,
                         'generic task creation selects PSRAM in this board profile')


if __name__ == '__main__':
    unittest.main()
