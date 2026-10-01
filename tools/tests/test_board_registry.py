import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from board_registry import ManifestError, build_command, load_boards  # noqa: E402


class BoardRegistryTest(unittest.TestCase):
    def make_root(self) -> Path:
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        root = Path(temp.name)
        (root / "product" / "interaction").mkdir(parents=True)
        (root / "product" / "interaction" / "profiles.json").write_text(
            json.dumps(
                {
                    "schema_version": 1,
                    "profiles": {
                        "headless-key": {"required_capabilities": []},
                        "touch-full": {"required_capabilities": ["display", "touch"]},
                    },
                    "layouts": {
                        "none": {"required_capabilities": []},
                        "compact": {"required_capabilities": ["display"]},
                    },
                }
            ),
            encoding="utf-8",
        )
        (root / "project" / "tools").mkdir(parents=True)
        (root / "project" / "CMakeLists.txt").write_text("# test\n", encoding="utf-8")
        (root / "project" / "tools" / "build.sh").write_text("#!/bin/sh\n", encoding="utf-8")
        (root / "boards" / "vendor" / "model").mkdir(parents=True)
        (root / "boards" / "vendor" / "model" / "README.md").write_text(
            "# Test board\n", encoding="utf-8"
        )
        return root

    def manifest(self, **updates):
        data = {
            "schema_version": 1,
            "id": "vendor-model",
            "vendor": "Vendor",
            "model": "Model",
            "revision": None,
            "platform": "esp-idf",
            "target": "esp32s3",
            "sdk": {"name": "ESP-IDF", "version": "5.5.4"},
            "project_dir": "project",
            "capabilities": ["audio"],
            "resources": {"flash_bytes": None, "psram_bytes": None},
            "variants": [
                {
                    "name": "default",
                    "default": True,
                    "interaction_profile": "headless-key",
                    "layout_profile": "none",
                }
            ],
            "migration": {"stage": "legacy-project"},
        }
        data.update(updates)
        return data

    def write_manifest(self, root: Path, data) -> None:
        path = root / "boards" / "vendor" / "model" / "board.json"
        path.write_text(json.dumps(data), encoding="utf-8")

    def test_loads_valid_manifest_and_selects_esp_idf(self):
        root = self.make_root()
        self.write_manifest(root, self.manifest())
        board = load_boards(root)[0]
        command, cwd = build_command(root, board, "default")
        self.assertEqual(command, ["idf.py", "build"])
        self.assertEqual(cwd, root / "project")

    def test_selects_beken_build_adapter(self):
        root = self.make_root()
        self.write_manifest(root, self.manifest(platform="beken", target="bk7258"))
        board = load_boards(root)[0]
        command, _ = build_command(root, board, "default")
        self.assertEqual(command, ["bash", "tools/build.sh"])

    def test_rejects_cross_platform_target(self):
        root = self.make_root()
        self.write_manifest(root, self.manifest(platform="beken", target="esp32s3"))
        with self.assertRaisesRegex(ManifestError, "does not belong"):
            load_boards(root)

    def test_rejects_project_path_escape(self):
        root = self.make_root()
        self.write_manifest(root, self.manifest(project_dir="../outside"))
        with self.assertRaisesRegex(ManifestError, "escapes the repository"):
            load_boards(root)

    def test_rejects_multiple_default_variants(self):
        root = self.make_root()
        self.write_manifest(
            root,
            self.manifest(
                variants=[
                    {
                        "name": "one",
                        "default": True,
                        "interaction_profile": "headless-key",
                        "layout_profile": "none",
                    },
                    {
                        "name": "two",
                        "default": True,
                        "interaction_profile": "headless-key",
                        "layout_profile": "none",
                    },
                ]
            ),
        )
        with self.assertRaisesRegex(ManifestError, "exactly one variant"):
            load_boards(root)

    def test_rejects_interaction_profile_without_hardware_capability(self):
        root = self.make_root()
        self.write_manifest(
            root,
            self.manifest(
                variants=[
                    {
                        "name": "default",
                        "default": True,
                        "interaction_profile": "touch-full",
                        "layout_profile": "compact",
                    }
                ]
            ),
        )
        with self.assertRaisesRegex(ManifestError, "requires capabilities"):
            load_boards(root)

    def test_rejects_headless_interaction_with_display_layout(self):
        root = self.make_root()
        self.write_manifest(
            root,
            self.manifest(
                capabilities=["audio", "display"],
                variants=[
                    {
                        "name": "default",
                        "default": True,
                        "interaction_profile": "headless-key",
                        "layout_profile": "compact",
                    }
                ],
            ),
        )
        with self.assertRaisesRegex(ManifestError, "requires the none layout"):
            load_boards(root)

    def test_rejects_display_interaction_without_display_layout(self):
        root = self.make_root()
        self.write_manifest(
            root,
            self.manifest(
                capabilities=["audio", "display", "touch"],
                variants=[
                    {
                        "name": "default",
                        "default": True,
                        "interaction_profile": "touch-full",
                        "layout_profile": "none",
                    }
                ],
            ),
        )
        with self.assertRaisesRegex(ManifestError, "requires a display layout"):
            load_boards(root)

    def test_rejects_unknown_build_variant(self):
        root = self.make_root()
        self.write_manifest(root, self.manifest())
        board = load_boards(root)[0]
        with self.assertRaisesRegex(ManifestError, "unknown variant"):
            build_command(root, board, "missing")


if __name__ == "__main__":
    unittest.main()
