"""Removed hardware must not remain selectable through the public build CLI."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]

class RetiredBoardTest(unittest.TestCase):
    def test_removed_board_is_not_listed_or_buildable(self):
        listing = subprocess.run([sys.executable, str(ROOT/'tools/build.py'), '--list'],
                                 capture_output=True, text=True, check=True)
        self.assertNotIn('alientek-atk-dnesp32s3', listing.stdout)
        result = subprocess.run([sys.executable, str(ROOT/'tools/build.py'),
                                 '--board', 'alientek-atk-dnesp32s3', '--dry-run'],
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('unknown board', (result.stdout + result.stderr).lower())

if __name__ == '__main__':
    unittest.main()
