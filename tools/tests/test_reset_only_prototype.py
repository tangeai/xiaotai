"""Check the touch prototype's network and reset entry points."""

from pathlib import Path
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[2]


class ResetOnlyPrototypeTests(unittest.TestCase):
    def test_network_page_has_no_separate_provisioning_action(self):
        script = ROOT / "docs/product/prototype.js"
        result = subprocess.run(
            ["node", "-e", """
const fs = require('fs');
const vm = require('vm');
const source = fs.readFileSync(process.argv[1], 'utf8');
const line = source.split('\\n').find(line => line.startsWith('function network(){'));
const page = vm.runInNewContext(`${line}; network()`);
if (!page.includes('网络状态良好') || page.includes('重新配网') ||
    page.includes('data-go="network-setup"')) process.exit(1);
""", str(script)],
            capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_reset_is_reachable_and_confirms_before_setup(self):
        script = ROOT / "docs/product/prototype.js"
        result = subprocess.run(
            ["node", "-e", """
const fs = require('fs');
const vm = require('vm');
const source = fs.readFileSync(process.argv[1], 'utf8');
const lines = source.split('\\n').filter(line =>
    line.startsWith('function settings(){') || line.startsWith('function resetConfirm(){'));
const pages = vm.runInNewContext(`${lines.join('\\n')}; [settings(), resetConfirm()]`, {
  head: title => `<h2>${title}</h2>`, speakerMuted: false, volume: 7,
  sleepAfterMinutes: 5, micMuted: false,
});
if (!pages[0].includes('data-go="reset-confirm"') ||
    !pages[1].includes('data-go="settings"') ||
    !pages[1].includes('data-go="network-setup"') ||
    !pages[1].includes('设备密钥')) process.exit(1);
""", str(script)],
            capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_network_info_stays_under_settings_and_setup_leads_to_binding(self):
        script = ROOT / "docs/product/prototype.js"
        result = subprocess.run(
            ["node", "-e", """
const fs = require('fs');
const vm = require('vm');
const source = fs.readFileSync(process.argv[1], 'utf8');
const names = ['deviceMenu', 'network', 'apSetup'];
const lines = source.split('\\n').filter(line => names.some(name =>
    line.startsWith(`function ${name}(){`)));
const pages = vm.runInNewContext(`${lines.join('\\n')}; [deviceMenu(), network(), apSetup()]`, {
  deviceHotspotName: 'XiaoTai-A1B2',
});
if (!pages[0].includes('运行状态') || pages[0].includes('data-go="network"') ||
    !pages[1].includes('data-go="settings"') ||
    !pages[2].includes('data-go="binding"')) process.exit(1);
""", str(script)],
            capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
