import json
from pathlib import Path
import re
import subprocess
import unittest
from urllib.parse import unquote, urlparse


ROOT = Path(__file__).resolve().parents[2]


class RepositoryLayoutTests(unittest.TestCase):
    def test_lckfb_board_ids_and_project_paths_use_public_names(self):
        expected = {
            "lckfb-esp32s3": (
                "boards/lckfb/esp32s3/board.json",
                "firmware/esp-idf/esp32s3/lckfb-esp32s3-xiaotai",
            ),
            "lckfb-bk7258": (
                "boards/lckfb/bk7258/board.json",
                "firmware/beken/bk7258/lckfb-bk7258-xiaotai",
            ),
        }
        for board_id, (manifest_path, project_dir) in expected.items():
            manifest = json.loads(
                (ROOT / manifest_path).read_text(encoding="utf-8")
            )
            self.assertEqual(manifest["id"], board_id)
            self.assertEqual(manifest["project_dir"], project_dir)
            self.assertTrue((ROOT / project_dir).is_dir(), project_dir)
            self.assertTrue(
                (ROOT / "docs/boards" / board_id / "README.md").is_file(),
                board_id,
            )

        for obsolete_path in (
            "boards/lckfb/szpi-esp32s3",
            "boards/lckfb/bk7258-touch",
            "docs/boards/lckfb-szpi-esp32s3-v101",
            "docs/boards/lckfb-bk7258-touch",
            "firmware/esp-idf/esp32s3/lckfb-szpi",
        ):
            self.assertFalse((ROOT / obsolete_path).exists(), obsolete_path)

        public_index = (
            (ROOT / "README.md").read_text(encoding="utf-8")
            + (ROOT / "docs/boards/README.md").read_text(encoding="utf-8")
        )
        for obsolete_id in (
            "lckfb-szpi-esp32s3-v101",
            "lckfb-bk7258-touch",
        ):
            self.assertNotIn(obsolete_id, public_index, obsolete_id)

        esp32s3_project = ROOT / expected["lckfb-esp32s3"][1]
        cmake = (esp32s3_project / "CMakeLists.txt").read_text(encoding="utf-8")
        project_readme = (esp32s3_project / "README.md").read_text(
            encoding="utf-8"
        )
        hardware_ir = json.loads(
            (esp32s3_project / "hardware-ir.json").read_text(encoding="utf-8")
        )
        self.assertIn("project(lckfb_esp32s3_xiaotai)", cmake)
        self.assertIn("# lckfb_esp32s3_xiaotai", project_readme)
        self.assertEqual(hardware_ir["board"]["id"], "lckfb_esp32s3")
        for artifact in hardware_ir["build_evidence"]["artifacts"]:
            self.assertTrue(
                artifact["path"].startswith("build/lckfb_esp32s3_xiaotai."),
                artifact["path"],
            )

    def test_legacy_document_buckets_are_absent(self):
        for relative in (
            "docs/小钛产品文档",
            "docs/参考资料与代码",
            "docs/官网开发板目录",
            "docs/nihaoxiaotai",
        ):
            self.assertFalse((ROOT / relative).exists(), relative)

    def test_stable_document_entries_exist(self):
        required = (
            "README.md",
            "ARCHITECTURE.md",
            "CONTRIBUTING.md",
            "docs/product/README.md",
            "docs/product/MEDIA_CONTRACT.md",
            "tests/README.md",
            "docs/boards/README.md",
            "docs/glossary.md",
        )
        for relative in required:
            self.assertTrue((ROOT / relative).is_file(), relative)

    def test_root_documents_share_navigation(self):
        navigation = (
            "[快速体验](README.md) · "
            "[架构与二次开发](ARCHITECTURE.md) · "
            "[参与开发](CONTRIBUTING.md) · "
            "[完整文档](docs/README.md)"
        )
        for relative in ("README.md", "ARCHITECTURE.md", "CONTRIBUTING.md"):
            lines = (ROOT / relative).read_text(encoding="utf-8").splitlines()
            self.assertGreaterEqual(len(lines), 3, relative)
            self.assertEqual(lines[2], navigation, relative)

    def test_root_readme_features_two_boards_and_catalog_lists_every_board(self):
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        catalog = (ROOT / "docs/boards/README.md").read_text(encoding="utf-8")
        featured = {
            "lckfb-bk7258",
            "lckfb-esp32s3",
        }
        for manifest in (ROOT / "boards").glob("*/*/board.json"):
            board_id = json.loads(manifest.read_text(encoding="utf-8"))["id"]
            self.assertIn(board_id, catalog, board_id)
            if board_id in featured:
                self.assertIn(board_id, readme, board_id)
            else:
                self.assertNotIn(board_id, readme, board_id)
        self.assertIn("[更多开发板](docs/boards/README.md)", readme)

    def test_root_documents_keep_reader_focused_section_order(self):
        expected_sections = {
            "README.md": (
                "## 选择开发板和固件",
                "## 烧录",
                "## 首次体验",
                "## 从源码编译",
                "## 下一步",
            ),
            "ARCHITECTURE.md": (
                "## 产品能力",
                "## 分层方式",
                "## 会话与媒体生命周期",
                "## 业务进入与会话优先级",
                "## 音视频合同",
                "## AEC 回声消除",
                "## 板卡、变体与证据",
                "## 二次开发路径",
                "## 验证方式",
                "## 继续阅读",
            ),
            "CONTRIBUTING.md": (
                "## 准备环境",
                "## 选择正确的开发方式",
                "## 手工开发与 AI 协作",
                "## 产品与媒体变更",
                "## 测试要求",
                "## 文档要求",
                "## 提交前检查",
            ),
        }
        for relative, sections in expected_sections.items():
            text = (ROOT / relative).read_text(encoding="utf-8")
            positions = [text.index(section) for section in sections]
            self.assertEqual(positions, sorted(positions), relative)

    def test_architecture_diagrams_stay_focused(self):
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        architecture = (ROOT / "ARCHITECTURE.md").read_text(encoding="utf-8")
        contributing = (ROOT / "CONTRIBUTING.md").read_text(encoding="utf-8")
        self.assertNotIn("```mermaid", readme)
        self.assertEqual(architecture.count("```mermaid"), 3)
        self.assertEqual(contributing.count("```mermaid"), 1)

    def test_product_session_admission_contract_is_explicit(self):
        architecture = (ROOT / "ARCHITECTURE.md").read_text(encoding="utf-8")
        product = (ROOT / "docs/product/PRODUCT_REQUIREMENTS.md").read_text(
            encoding="utf-8"
        )
        for text in (architecture, product):
            self.assertIn("H5", text)
            self.assertIn("多人对讲", text)
            self.assertIn("待接听", text)
            self.assertIn("用户", text)
            self.assertIn("媒体所有权", text)
        self.assertIn("不立即停止当前业务", architecture)
        self.assertIn("用户接听", product)

    def test_media_contract_keeps_required_protocol_details(self):
        contract = (ROOT / "docs/product/MEDIA_CONTRACT.md").read_text(encoding="utf-8")
        required_details = (
            "H5 实时查看与对讲",
            "音频 10、视频 11",
            "音频 14、视频 15",
            "微信通话",
            "音频 0、视频 1",
            "PCM",
            "Opus",
            "AEC",
            "640×480",
            "1280×960",
            "wechat_voip_media.c",
        )
        for detail in required_details:
            self.assertIn(detail, contract, detail)

    def test_product_prototype_matches_product_contract(self):
        product_index = (ROOT / "docs/product/README.md").read_text(encoding="utf-8")
        prototype_html = (ROOT / "docs/product/prototype.html").read_text(
            encoding="utf-8"
        )
        prototype_js = (ROOT / "docs/product/prototype.js").read_text(
            encoding="utf-8"
        )

        for product_shape in ("无屏设备", "有屏但无触摸", "小屏触摸设备", "大屏触摸设备"):
            self.assertIn(product_shape, product_index)
        self.assertIn("产品文档与交互原型", product_index)

        self.assertNotIn("P4 规划预览", prototype_html)
        self.assertNotIn("当前仓库：ESP32-S3", prototype_html)
        self.assertNotIn("quickWechatCall", prototype_js)
        self.assertIn("quickFirstContactCall", prototype_js)
        self.assertIn("visibleContacts()[0]", prototype_js)
        for stream_contract in (
            "音频 10、视频 11",
            "音频 14、视频 15",
            "音频 0、视频 1",
        ):
            self.assertIn(stream_contract, prototype_js)

    def test_every_board_has_a_user_guide(self):
        board_ids = (
            "alientek-atk-dnesp32s3",
            "lckfb-bk7258",
            "lckfb-esp32s3",
            "waveshare-esp32p4-touch-lcd-43c-v10",
        )
        for board_id in board_ids:
            guide = ROOT / "docs/boards" / board_id / "README.md"
            self.assertTrue(guide.is_file(), str(guide))

    def test_removed_p4_35_board_is_not_published(self):
        removed_paths = (
            "boards/waveshare/esp32p4-touch-lcd-35",
            "docs/boards/waveshare-esp32p4-touch-lcd-35",
            "firmware/esp-idf/esp32p4/waveshare-touch-lcd-35",
        )
        for relative in removed_paths:
            self.assertFalse((ROOT / relative).exists(), relative)

        public_indexes = (
            ROOT / "README.md",
            ROOT / "docs/boards/README.md",
        )
        for document in public_indexes:
            text = document.read_text(encoding="utf-8")
            self.assertNotIn("esp32p4-touch-lcd-35", text, str(document))

    def test_every_board_guide_separates_runtime_status_from_evidence_todos(self):
        board_ids = (
            "alientek-atk-dnesp32s3",
            "lckfb-bk7258",
            "lckfb-esp32s3",
            "waveshare-esp32p4-touch-lcd-43c-v10",
        )
        for board_id in board_ids:
            guide = ROOT / "docs/boards" / board_id / "README.md"
            text = guide.read_text(encoding="utf-8")
            self.assertIn("实板", text, str(guide))
            self.assertIn("TODO", text, str(guide))

    def test_every_board_guide_has_required_user_sections(self):
        required_sections = (
            "## 烧录",
            "## 首次使用",
            "## 规格",
            "## 音视频参数",
            "## 环境与依赖",
            "## 原理图与硬件资料",
            "## 开发",
            "## 编译",
            "## 测试",
            "## 排查",
            "## 验证状态与已知限制",
        )
        for manifest in (ROOT / "boards").glob("*/*/board.json"):
            document = json.loads(manifest.read_text(encoding="utf-8"))
            board_id = document["id"]
            guide = ROOT / "docs" / "boards" / board_id / "README.md"
            self.assertTrue(guide.is_file(), str(guide))
            text = guide.read_text(encoding="utf-8")
            for section in required_sections:
                self.assertIn(section, text, f"{guide}: missing {section}")
            positions = [text.index(section) for section in required_sections]
            self.assertEqual(
                positions,
                sorted(positions),
                f"{guide}: user experience must come before development",
            )

    def test_board_index_matches_manifest_memory(self):
        index = (ROOT / "docs/boards/README.md").read_text(encoding="utf-8")
        self.assertIn("32 MB Flash / 32 MB PSRAM", index)
        public_files = [ROOT / "README.md"]
        public_files.extend(
            path for path in (ROOT / "docs").rglob("*.md")
            if ".local" not in path.parts
        )
        public_files.extend((ROOT / "docs/product/board-catalog").glob("*.html"))
        for path in public_files:
            self.assertNotIn(
                "ESP32-P4-WIFI6-Touch-LCD-3.5",
                path.read_text(encoding="utf-8"),
                f"obsolete public board model in {path.relative_to(ROOT)}",
            )

    def test_lckfb_esp32s3_identity_separates_board_module_and_revision(self):
        guide = ROOT / "docs/boards/lckfb-esp32s3/README.md"
        text = guide.read_text(encoding="utf-8")
        self.assertIn("# 立创·实战派 ESP32-S3", text)
        self.assertIn("ESP32-S3-WROOM-1-N16R8", text)
        self.assertIn("PCB V1.0.1", text)

        hardware_ir = json.loads((
            ROOT / "firmware/esp-idf/esp32s3/lckfb-esp32s3-xiaotai/hardware-ir.json"
        ).read_text(encoding="utf-8"))
        self.assertEqual(
            hardware_ir["soc"]["module"], "ESP32-S3-WROOM-1-N16R8"
        )

    def test_product_name_uses_xiaotai_titanium_character(self):
        for path in (ROOT / "docs").rglob("*.md"):
            text = path.read_text(encoding="utf-8")
            self.assertNotIn("小泰", text, f"{path}: product name must be 小钛")

    def test_bk7258_schematic_is_indexed_with_checksum(self):
        guide = ROOT / "docs/boards/lckfb-bk7258/README.md"
        text = guide.read_text(encoding="utf-8")
        self.assertIn("ATK-DNT5M_V1.0+原理图.PDF", text)
        self.assertIn(
            "faf535aea976d599d1576f21ff07d38600486b01fc18925c8bdc7f024c5203ef",
            text,
        )

    def test_p4_43c_schematic_is_indexed_with_checksum(self):
        guide = ROOT / "docs/boards/waveshare-esp32p4-touch-lcd-43c-v10/README.md"
        text = guide.read_text(encoding="utf-8")
        self.assertIn(
            "https://docs.waveshare.net/ESP32-P4-WIFI6-Touch-LCD-4.3/",
            text,
        )
        self.assertIn("ESP32-P4-WIFI6-Touch-LCD-4.3-schematic.pdf", text)
        self.assertIn(
            "3697baa3ded0089446baf09705f437d13cf0324874031ccc57fd9b72cd9dfe53",
            text,
        )

    def test_local_only_directories_are_ignored_and_reference_payloads_are_untracked(self):
        ignore = (ROOT / ".gitignore").read_text(encoding="utf-8")
        self.assertIn("**/.local/", ignore)
        self.assertIn("/.references/", ignore)
        self.assertNotIn("/references/.local/", ignore)
        self.assertNotIn("/docs/.local/", ignore)
        self.assertNotIn("/tests/.local/", ignore)
        self.assertFalse((ROOT / "references").exists())

    def test_board_guides_are_self_contained_for_development(self):
        self.assertFalse((ROOT / "docs/development/toolchains.md").exists())
        self.assertFalse((ROOT / "docs/development/toolchains").exists())
        self.assertFalse((ROOT / "docs/development/board-dependencies.md").exists())

        expected = {
            "alientek-atk-dnesp32s3": (
                "735507283d5b2f9fb363a1901172dbd9e847945d",
                "xtensa-esp-elf-gcc 14.2.0",
                "7334e846ed4261b5297607c85acafe1d586a22374f7c8e37b102b2742e47c7aa",
            ),
            "lckfb-esp32s3": (
                "735507283d5b2f9fb363a1901172dbd9e847945d",
                "xtensa-esp-elf-gcc 14.2.0",
                "43b06d1da421c7d24cc7fdb1385d600ecdffbfd2d3801f7faf0c540fb5cdbaa2",
            ),
            "waveshare-esp32p4-touch-lcd-43c-v10": (
                "735507283d5b2f9fb363a1901172dbd9e847945d",
                "riscv32-esp-elf-gcc 14.2.0",
                "a7a01ffd496a55364c7e4d665ff3884d078147bba96752a965d97befca12e451",
            ),
            "lckfb-bk7258": (
                "1cfd56af09a3cb6470f35f1e0c604035ed1b6ee7",
                "arm-none-eabi-gcc 10.3.1",
                "47e26827ca2419084163e3656dca4d6e508e433381784e9c03f232d3dbaa4171",
            ),
        }
        for board_id, facts in expected.items():
            board_guide = ROOT / "docs/boards" / board_id / "README.md"
            text = board_guide.read_text(encoding="utf-8")
            for heading in ("## 开发", "## 编译", "## 烧录", "## 测试", "## 排查"):
                self.assertIn(heading, text, f"{board_guide}: missing {heading}")
            for fact in facts:
                self.assertIn(fact, text, f"{board_guide}: missing {fact}")

    def test_esp_idf_setup_selects_one_chip_or_explicit_all(self):
        setup = (ROOT / "tools/setup_esp_idf.sh").read_text(encoding="utf-8")
        self.assertIn('esp32s3|esp32p4|all', setup)
        self.assertIn('install_targets="esp32s3,esp32p4"', setup)

        result = subprocess.run(
            ["bash", str(ROOT / "tools/setup_esp_idf.sh")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("{esp32s3|esp32p4|all}", result.stderr)

    def test_local_test_reports_are_ignored(self):
        ignore = (ROOT / ".gitignore").read_text(encoding="utf-8")
        self.assertIn("**/.local/", ignore)
        self.assertFalse((ROOT / "capability-assessment.json").exists())
        self.assertFalse((ROOT / "doctor.json").exists())

    def test_default_check_enforces_product_coverage(self):
        check = (ROOT / "tools/check.sh").read_text(encoding="utf-8")
        coverage = (ROOT / "tools/product_coverage.py").read_text(
            encoding="utf-8"
        )
        contributing = (ROOT / "CONTRIBUTING.md").read_text(encoding="utf-8")
        self.assertIn('python3 "$repo_dir/tools/product_coverage.py"', check)
        self.assertIn('default=88.0', coverage)
        self.assertIn('default=57.0', coverage)
        self.assertIn('"xiaotai_runtime.c": (95.0, 65.0)', coverage)
        self.assertIn('"xiaotai_call_state.c": (95.0, 60.0)', coverage)
        self.assertIn('"xiaotai_room.c": (80.0, 50.0)', coverage)
        self.assertIn("测试驱动开发", contributing)

    def test_change_workflow_requires_test_evidence(self):
        agents = (ROOT / "AGENTS.md").read_text(encoding="utf-8")
        template_path = ROOT / ".github/pull_request_template.md"
        self.assertTrue(template_path.is_file())
        template = template_path.read_text(encoding="utf-8")

        for phrase in (
            "changing behavior",
            "fixing a defect",
            "fails before the implementation change",
            "tools/check.sh",
        ):
            self.assertIn(phrase, agents)
        for phrase in ("测试证据", "修改前失败", "完整检查", "实板验证"):
            self.assertIn(phrase, template)

    def test_local_work_notes_are_not_part_of_public_docs(self):
        ignore = (ROOT / ".gitignore").read_text(encoding="utf-8")
        index = (ROOT / "docs/README.md").read_text(encoding="utf-8")
        self.assertIn("**/.local/", ignore)
        self.assertNotIn("docs/.local", index)
        self.assertNotIn("history/README.md", index)
        self.assertNotIn("refactor-status-", index)

    def test_public_documents_do_not_have_wall_of_text_paragraphs(self):
        documents = [
            ROOT / "README.md",
            ROOT / "ARCHITECTURE.md",
            ROOT / "CONTRIBUTING.md",
        ]
        documents.extend((ROOT / "docs").glob("*.md"))
        for section in ("boards", "development", "product"):
            documents.extend((ROOT / "docs" / section).rglob("*.md"))
        documents.extend((ROOT / "boards").glob("*/*/README.md"))

        for document in documents:
            if ".local" in document.parts:
                continue
            paragraphs = []
            current = []
            in_code = False
            for line in document.read_text(encoding="utf-8").splitlines() + [""]:
                if line.startswith("```"):
                    in_code = not in_code
                    current = []
                    continue
                is_list = re.match(r"^\s*(?:[-*+]|\d+\.)\s+", line)
                is_boundary = (
                    not line.strip()
                    or line.startswith("#")
                    or line.startswith("|")
                    or line.startswith(">")
                    or is_list
                )
                if in_code:
                    continue
                if is_boundary:
                    if current:
                        paragraphs.append(" ".join(part.strip() for part in current))
                        current = []
                else:
                    current.append(line)

            for paragraph in paragraphs:
                self.assertLessEqual(
                    len(paragraph),
                    320,
                    f"{document}: split long paragraph ({len(paragraph)} characters)",
                )

    def test_public_document_local_links_exist(self):
        documents = [
            ROOT / "README.md",
            ROOT / "ARCHITECTURE.md",
            ROOT / "CONTRIBUTING.md",
        ]
        documents.extend((ROOT / "docs").rglob("*.md"))
        documents.extend((ROOT / "boards").glob("*/*/README.md"))

        for document in documents:
            if ".local" in document.parts:
                continue
            text = document.read_text(encoding="utf-8")
            for raw_target in re.findall(r"\[[^]]+\]\(([^)]+)\)", text):
                target = raw_target.strip().strip("<>")
                parsed = urlparse(target)
                if parsed.scheme or not parsed.path:
                    continue
                linked_path = (document.parent / unquote(parsed.path)).resolve()
                self.assertTrue(
                    linked_path.exists(),
                    f"{document}: missing local link target {raw_target}",
                )

    def test_ignore_rules_do_not_hide_repository_metadata(self):
        ignore = (ROOT / ".gitignore").read_text(encoding="utf-8")
        self.assertNotIn("\n*.json\n", f"\n{ignore}\n")
        self.assertNotIn("\n**/*.csv\n", f"\n{ignore}\n")
        self.assertNotIn("\n**/build*/\n", f"\n{ignore}\n")
        self.assertIn("**/build/", ignore)
        self.assertIn("**/build-*/", ignore)

    def test_third_party_checklist_tracks_current_asset_locations(self):
        checklist = (ROOT / "THIRD_PARTY.md").read_text(encoding="utf-8")
        for expected in (
            "firmware/**/third_party/tirtc/",
            "platforms/esp-idf/components/starter_voice/vendor/",
            "product/assets/models/nihaoxiaotai/",
            "main/ui/font/lv_font_cn_14.c",
            "product/assets/audio/",
        ):
            self.assertIn(expected, checklist)
        self.assertNotIn("ui_font_cn_16.c", checklist)
        self.assertNotIn("TiRTC SDK 2.3.0", checklist)

    def test_hardware_ir_project_sources_are_repository_relative_and_exist(self):
        for hardware_ir in (ROOT / "firmware").rglob("hardware-ir.json"):
            document = json.loads(hardware_ir.read_text(encoding="utf-8"))
            for source in document.get("sources", []):
                if source.get("kind") != "project-source":
                    continue
                location = source.get("location", "")
                self.assertTrue(location, f"missing location in {hardware_ir}")
                self.assertFalse(
                    Path(location).is_absolute(),
                    f"project source must be repository-relative: {location}",
                )
                self.assertTrue(
                    (ROOT / location).is_file(),
                    f"missing project source referenced by {hardware_ir}: {location}",
                )


if __name__ == "__main__":
    unittest.main()
