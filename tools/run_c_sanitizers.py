#!/usr/bin/env python3
"""Run platform-neutral product-core tests under ASan and UBSan."""

from __future__ import annotations

from pathlib import Path
import os
import subprocess
import tempfile

from product_coverage import CASES, PRODUCT


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="xiaotai-sanitize-") as tmp:
        work = Path(tmp)
        for name, sources in CASES.items():
            output = work / name
            command = [
                "cc", "-std=c11", "-O1", "-g", "-Wall", "-Wextra",
                "-Werror", "-fno-omit-frame-pointer",
                "-fsanitize=address,undefined",
                "-I", str(PRODUCT / "include"),
                "-I", "/usr/include/cjson",
                *(str(PRODUCT / "src" / source) for source in sources),
                str(PRODUCT / "tests" / f"test_{name}.c"),
                "-lcjson", "-o", str(output),
            ]
            subprocess.run(command, check=True)
            env = os.environ.copy()
            env.update({
                "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
                "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1",
            })
            result = subprocess.run(
                [str(output)], env=env, text=True, capture_output=True
            )
            if (result.returncode != 0 and
                    "LeakSanitizer does not work under ptrace" in
                    result.stderr):
                # Codex/CI sandboxes may supervise children with ptrace. Keep
                # ASan/UBSan active there; leak detection remains enabled in
                # ordinary unsupervised runners.
                env["ASAN_OPTIONS"] = "detect_leaks=0:halt_on_error=1"
                subprocess.run([str(output)], check=True, env=env)
            elif result.returncode != 0:
                print(result.stdout, end="")
                print(result.stderr, end="")
                raise subprocess.CalledProcessError(
                    result.returncode, [str(output)]
                )
    print("Product-core ASan/UBSan checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
