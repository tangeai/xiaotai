"""Keep the BK7258 native build on one matched SDK and TiRTC package."""

import hashlib
import json
import os
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "firmware/beken/bk7258/lckfb-bk7258-xiaotai"
SDK = ROOT / ".tools/bk_avdk_smp-release-v3.1.1.10"
SDK_COMMIT = "398cab28b938e69ba2071a597daf00845dd080b6"
TIRTC_SHA256 = "ae5c9e85b8247c8a8d6ed2ef50e2c2199276c3e113d6a71c57e235d934dde968"


class BKSDKUpgradeTest(unittest.TestCase):
    def test_native_build_selects_matching_tirtc_package(self):
        archive = ROOT / "platforms/beken/third_party/tirtc/lib/libTiRTC.a"
        self.assertEqual(TIRTC_SHA256, hashlib.sha256(archive.read_bytes()).hexdigest())
        manifest = (ROOT / "platforms/beken/third_party/tirtc/manifest/build-info.env").read_text()
        self.assertIn("BUILD_PLATFORM_SDK_VERSION=3.1.1.10-official-398cab28", manifest)
        self.assertIn("SDK_VERSION=v2.6.1", manifest)
        self.assertTrue(b"v2.6.0\x00" in archive.read_bytes(), "delivered archive runtime version changed")
        build_script = (ROOT / "platforms/beken/tools/build.sh").read_text()
        self.assertTrue(f'expected_tirtc_sha256="{TIRTC_SHA256}"' in build_script, "native build must admit the delivered archive hash")
        self.assertIn("CRYPTO_COMPONENT=psa_mbedtls", manifest)
        self.assertIn("CRYPTO_VERSION=3.5.2", manifest)
        board = json.loads((ROOT / "boards/lckfb/bk7258/board.json").read_text())
        self.assertEqual("release/v3.1.1.10", board["sdk"]["version"])
        self.assertIn("release/v3.1.1.10", (PROJECT / "ap/ap_main.c").read_text())
        self.assertIn("release/v3.1.1.10", (PROJECT / "ap/tirtc_bk_probe.c").read_text())
        self.assertIn("bk_avdk_smp-release-v3.1.1.10", (PROJECT / "Makefile").read_text())
        self.assertIn("bk_avdk_smp-release-v3.1.1.10", (ROOT / "platforms/beken/tools/build.sh").read_text())
        setup = (PROJECT / "tools/setup_build_env.sh").read_text()
        self.assertIn(SDK_COMMIT, setup)
        self.assertIn("https://gitee.com/bekencorp/bk_avdk_smp.git", setup)
        self.assertIn('if [ ! -e "$sdk/.git" ]; then', setup)

    def test_installed_official_sdk_identity(self):
        if not (SDK / "tools/build_tools/build_files/project_main.mk").is_file():
            self.skipTest("BK SDK checkout is not installed")
        self.assertEqual(
            SDK_COMMIT,
            subprocess.check_output(
                ["git", "-C", str(SDK), "rev-parse", "HEAD"], text=True
            ).strip(),
        )
        self.assertEqual(
            0,
            subprocess.run(
                ["python3", str(PROJECT / "tools/doctor.py")],
                env={**os.environ, "SDK_DIR": str(SDK)},
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                check=False,
            ).returncode,
        )


if __name__ == "__main__":
    unittest.main()
