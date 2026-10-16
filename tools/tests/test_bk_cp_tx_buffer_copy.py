"""Verify the public AP-to-CP Wi-Fi TX path owns its CP-side packet copy."""

import os
import re
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / 'firmware/beken/bk7258/lckfb-bk7258-xiaotai'
AP_CONFIG = PROJECT / 'ap/config/bk7258_ap/config'
CP_CONFIG = PROJECT / 'cp/config/bk7258/config'
CP_ELF = PROJECT / 'build/bk7258/lckfb-bk7258-xiaotai/bk7258/app.elf'
OBJDUMP = ROOT / '.tools/gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-objdump'


class BKCPTransmitBufferCopyTest(unittest.TestCase):
    def test_ap_to_cp_tx_copies_packet_before_queueing(self):
        ap_config = AP_CONFIG.read_text()
        cp_config = CP_CONFIG.read_text()
        for config, setting in (
            (ap_config, 'CONFIG_CONTROLLER_AP_BUFFER_COPY=y'),
            (cp_config, 'CONFIG_CONTROLLER_AP_BUFFER_COPY=y'),
            (ap_config, 'CONFIG_LWIP_MEM_LIBC_MALLOC_USE_PSRAM=y'),
        ):
            self.assertIn(setting, config.splitlines(), setting)

        elf = Path(os.environ.get('BK_CP_ELF', CP_ELF))
        objdump = Path(os.environ.get('BK_OBJDUMP', OBJDUMP))
        if not elf.is_file() or not objdump.is_file():
            self.skipTest('built BK CP ELF and objdump are required')
        result = subprocess.run(
            [str(objdump), '-d', str(elf)],
            capture_output=True, text=True, check=True)
        match = re.search(r'<cif_handle_txdata>:\n(.*?)(?=\n[0-9a-f]+ <[^>]+>:)',
                          result.stdout, re.S)
        self.assertIsNotNone(match, 'CP transmit entry must remain inspectable')
        calls = re.findall(r'\bbl\s+[0-9a-f]+\s+<([^>]+)>', match.group(1))
        self.assertIn('pbuf_clone', calls,
                      'CP must clone the AP packet before asynchronous Wi-Fi TX')
        self.assertIn('cif_free_ap_txbuf', calls,
                      'CP must return the AP packet after cloning it')
        self.assertIn('bmsg_tx_sender', calls,
                      'CP must submit the cloned packet to the Wi-Fi queue')
        self.assertLess(calls.index('pbuf_clone'), calls.index('bmsg_tx_sender'))


if __name__ == '__main__':
    unittest.main()
