#!/usr/bin/env python3
from pathlib import Path
import os
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_SDK_TAG = "release/v3.1.1.8"
EXPECTED_SDK_COMMIT = "1cfd56af09a3cb6470f35f1e0c604035ed1b6ee7"


def report(ok: bool, label: str, detail: str) -> None:
    print(f"{'PASS' if ok else 'FAIL'} {label}: {detail}")


sdk_arg = os.environ.get("SDK_DIR")
sdk_candidates = []
if sdk_arg:
    sdk_candidates.append(Path(sdk_arg).expanduser())
sdk_candidates.extend([
    ROOT.parent / ".tools/bk_avdk_smp-release-v3.1.1.8",
])
sdk = next((p.resolve() for p in sdk_candidates
            if (p / "tools/build_tools/build_files/project_main.mk").is_file()), None)
report(sdk is not None, "BK-AVDK build entry", str(sdk or sdk_candidates[0]))
sdk_commit = None
if sdk is not None and (sdk / ".git").exists():
    sdk_commit = subprocess.run(
        ["git", "-C", str(sdk), "rev-parse", "HEAD"],
        check=False, capture_output=True, text=True,
    ).stdout.strip()
sdk_identity_ok = sdk_commit == EXPECTED_SDK_COMMIT
report(sdk_identity_ok, "BK-AVDK identity",
       f"{EXPECTED_SDK_TAG} expected={EXPECTED_SDK_COMMIT} actual={sdk_commit or 'unknown'}")

compiler = shutil.which("arm-none-eabi-gcc")
if compiler is None:
    local_compiler = (ROOT.parent / ".tools/gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-gcc")
    if local_compiler.is_file():
        compiler = str(local_compiler)
report(compiler is not None, "GCC Arm None EABI", compiler or "not installed")

python_env = ROOT.parent / ".tools/bk-python/bin/python"
report(python_env.is_file(), "BK-AVDK Python environment", str(python_env))

ok = sdk is not None and sdk_identity_ok and compiler is not None and python_env.is_file()
if not ok:
    print("Target build is unavailable; host contract checks remain usable.")
sys.exit(0 if ok else 1)
