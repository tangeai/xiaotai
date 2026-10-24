#!/usr/bin/env python3
"""Exercise the selected board's public host contracts after native linking."""
from pathlib import Path
import subprocess
import sys
root = Path(__file__).resolve().parents[5]
subprocess.run([sys.executable, str(root/'tools/tests/test_mgkfb_board.py')], check=True)
