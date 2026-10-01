#!/usr/bin/env python3
"""Board manifest discovery, validation, and platform build dispatch."""

from __future__ import annotations

import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


ID_PATTERN = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
PLATFORM_TARGETS = {
    "esp-idf": {"esp32s3", "esp32p4"},
    "beken": {"bk7258", "bk7259"},
}
MIGRATION_STAGES = {"legacy-project", "platform-project", "board-adapter"}


class ManifestError(ValueError):
    pass


@dataclass(frozen=True)
class Board:
    manifest_path: Path
    data: dict[str, Any]

    @property
    def id(self) -> str:
        return self.data["id"]

    @property
    def project_dir(self) -> str:
        return self.data["project_dir"]

    def default_variant(self) -> str:
        defaults = [item["name"] for item in self.data["variants"] if item["default"]]
        if len(defaults) != 1:
            raise ManifestError(f"{self.id}: exactly one variant must be default")
        return defaults[0]

    def has_variant(self, name: str) -> bool:
        return any(item["name"] == name for item in self.data["variants"])

    def project_path(self, root: Path) -> Path:
        """Return the validated project directory for this board."""
        return _resolve_inside(root, self.project_dir, "project_dir")


def _require_type(data: dict[str, Any], name: str, expected: type) -> Any:
    value = data.get(name)
    if not isinstance(value, expected):
        raise ManifestError(f"{name} must be {expected.__name__}")
    return value


def _resolve_inside(root: Path, relative: str, label: str) -> Path:
    path = Path(relative)
    if path.is_absolute():
        raise ManifestError(f"{label} must be relative to the repository")
    resolved = (root / path).resolve()
    try:
        resolved.relative_to(root.resolve())
    except ValueError as exc:
        raise ManifestError(f"{label} escapes the repository: {relative}") from exc
    return resolved


def load_interaction_profiles(root: Path) -> dict[str, dict[str, Any]]:
    path = root / "product" / "interaction" / "profiles.json"
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ManifestError(f"cannot load interaction profiles: {exc}") from exc
    if not isinstance(data, dict) or data.get("schema_version") != 1:
        raise ManifestError("interaction profile registry must use schema_version 1")
    profiles = data.get("profiles")
    if not isinstance(profiles, dict) or not profiles:
        raise ManifestError("interaction profile registry is empty")
    for name, profile in profiles.items():
        if not isinstance(name, str) or not ID_PATTERN.fullmatch(name):
            raise ManifestError(f"invalid interaction profile name: {name}")
        if not isinstance(profile, dict):
            raise ManifestError(f"interaction profile {name} must be an object")
        required = profile.get("required_capabilities")
        if not isinstance(required, list) or not all(isinstance(item, str) for item in required):
            raise ManifestError(f"interaction profile {name} has invalid required_capabilities")
    return profiles


def load_layout_profiles(root: Path) -> dict[str, dict[str, Any]]:
    path = root / "product" / "interaction" / "profiles.json"
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ManifestError(f"cannot load layout profiles: {exc}") from exc
    layouts = data.get("layouts") if isinstance(data, dict) else None
    if not isinstance(layouts, dict) or not layouts:
        raise ManifestError("layout profile registry is empty")
    for name, layout in layouts.items():
        if not isinstance(name, str) or not ID_PATTERN.fullmatch(name):
            raise ManifestError(f"invalid layout profile name: {name}")
        if not isinstance(layout, dict):
            raise ManifestError(f"layout profile {name} must be an object")
        required = layout.get("required_capabilities")
        if not isinstance(required, list) or not all(isinstance(item, str) for item in required):
            raise ManifestError(f"layout profile {name} has invalid required_capabilities")
    return layouts


def validate_manifest(
    root: Path,
    path: Path,
    data: dict[str, Any],
    interaction_profiles: dict[str, dict[str, Any]],
    layout_profiles: dict[str, dict[str, Any]],
) -> Board:
    required = {
        "schema_version", "id", "vendor", "model", "revision", "platform",
        "target", "sdk", "project_dir", "capabilities", "resources", "variants",
        "migration",
    }
    optional = {"evidence"}
    missing = sorted(required - data.keys())
    unknown = sorted(data.keys() - required - optional)
    if missing:
        raise ManifestError(f"missing fields: {', '.join(missing)}")
    if unknown:
        raise ManifestError(f"unknown fields: {', '.join(unknown)}")
    if data["schema_version"] != 1:
        raise ManifestError("schema_version must be 1")

    board_id = _require_type(data, "id", str)
    if not ID_PATTERN.fullmatch(board_id):
        raise ManifestError("id must be lowercase kebab-case")
    for field in ("vendor", "model", "project_dir"):
        if not _require_type(data, field, str).strip():
            raise ManifestError(f"{field} must not be empty")
    if data["revision"] is not None and not isinstance(data["revision"], str):
        raise ManifestError("revision must be a string or null")

    platform = _require_type(data, "platform", str)
    target = _require_type(data, "target", str)
    if platform not in PLATFORM_TARGETS:
        raise ManifestError(f"unsupported platform: {platform}")
    if target not in PLATFORM_TARGETS[platform]:
        raise ManifestError(f"target {target} does not belong to platform {platform}")

    sdk = _require_type(data, "sdk", dict)
    if set(sdk) != {"name", "version"} or not all(
        isinstance(sdk[key], str) and sdk[key].strip() for key in ("name", "version")
    ):
        raise ManifestError("sdk requires non-empty name and version")

    capabilities = _require_type(data, "capabilities", list)
    if len(capabilities) != len(set(capabilities)) or not all(
        isinstance(item, str) and ID_PATTERN.fullmatch(item) for item in capabilities
    ):
        raise ManifestError("capabilities must be unique lowercase kebab-case strings")

    resources = _require_type(data, "resources", dict)
    if set(resources) != {"flash_bytes", "psram_bytes"}:
        raise ManifestError("resources requires only flash_bytes and psram_bytes")
    for key, value in resources.items():
        if value is not None and (not isinstance(value, int) or isinstance(value, bool) or value <= 0):
            raise ManifestError(f"resources.{key} must be a positive integer or null")

    variants = _require_type(data, "variants", list)
    if not variants:
        raise ManifestError("variants must not be empty")
    variant_names: list[str] = []
    default_count = 0
    for variant in variants:
        if not isinstance(variant, dict) or set(variant) != {
            "name", "default", "interaction_profile", "layout_profile"
        }:
            raise ManifestError(
                "each variant requires name, default, interaction_profile, and layout_profile"
            )
        name = variant["name"]
        if not isinstance(name, str) or not ID_PATTERN.fullmatch(name):
            raise ManifestError("variant name must be lowercase kebab-case")
        if not isinstance(variant["default"], bool):
            raise ManifestError("variant default must be boolean")
        interaction_profile = variant["interaction_profile"]
        if interaction_profile not in interaction_profiles:
            raise ManifestError(f"unknown interaction profile: {interaction_profile}")
        required_capabilities = set(
            interaction_profiles[interaction_profile]["required_capabilities"]
        )
        missing_capabilities = sorted(required_capabilities - set(capabilities))
        if missing_capabilities:
            raise ManifestError(
                f"interaction profile {interaction_profile} requires capabilities: "
                f"{', '.join(missing_capabilities)}"
            )
        layout_profile = variant["layout_profile"]
        if layout_profile not in layout_profiles:
            raise ManifestError(f"unknown layout profile: {layout_profile}")
        layout_capabilities = set(layout_profiles[layout_profile]["required_capabilities"])
        missing_layout_capabilities = sorted(layout_capabilities - set(capabilities))
        if missing_layout_capabilities:
            raise ManifestError(
                f"layout profile {layout_profile} requires capabilities: "
                f"{', '.join(missing_layout_capabilities)}"
            )
        if interaction_profile == "headless-key" and layout_profile != "none":
            raise ManifestError("headless-key interaction requires the none layout")
        if interaction_profile != "headless-key" and layout_profile == "none":
            raise ManifestError(f"{interaction_profile} interaction requires a display layout")
        variant_names.append(name)
        default_count += int(variant["default"])
    if len(variant_names) != len(set(variant_names)):
        raise ManifestError("variant names must be unique")
    if default_count != 1:
        raise ManifestError("exactly one variant must be default")

    migration = _require_type(data, "migration", dict)
    if set(migration) != {"stage"} or migration["stage"] not in MIGRATION_STAGES:
        raise ManifestError("migration.stage is invalid")

    project_dir = _resolve_inside(root, data["project_dir"], "project_dir")
    if not project_dir.is_dir():
        raise ManifestError(f"project_dir does not exist: {data['project_dir']}")
    required_marker = "CMakeLists.txt"
    if not (project_dir / required_marker).is_file():
        raise ManifestError(f"project_dir is missing {required_marker}")
    if platform == "beken" and not (project_dir / "tools" / "build.sh").is_file():
        raise ManifestError("Beken project is missing tools/build.sh")
    if not (path.parent / "README.md").is_file():
        raise ManifestError("board directory is missing README.md")

    evidence = data.get("evidence", {})
    if not isinstance(evidence, dict) or not set(evidence).issubset({"hardware_ir", "probe_report"}):
        raise ManifestError("evidence supports only hardware_ir and probe_report")
    for label, relative in evidence.items():
        if not isinstance(relative, str) or not _resolve_inside(root, relative, f"evidence.{label}").is_file():
            raise ManifestError(f"evidence.{label} does not exist: {relative}")

    return Board(path, data)


def load_boards(root: Path) -> list[Board]:
    boards: list[Board] = []
    errors: list[str] = []
    interaction_profiles = load_interaction_profiles(root)
    layout_profiles = load_layout_profiles(root)
    for path in sorted((root / "boards").glob("*/*/board.json")):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
            if not isinstance(data, dict):
                raise ManifestError("manifest root must be an object")
            boards.append(
                validate_manifest(root, path, data, interaction_profiles, layout_profiles)
            )
        except (OSError, json.JSONDecodeError, ManifestError) as exc:
            errors.append(f"{path.relative_to(root)}: {exc}")
    if not boards and not errors:
        errors.append("no board manifests found under boards/<vendor>/<board>/board.json")

    seen: dict[str, Path] = {}
    projects: dict[str, Path] = {}
    for board in boards:
        if board.id in seen:
            errors.append(
                f"duplicate board id {board.id}: {seen[board.id].relative_to(root)} and "
                f"{board.manifest_path.relative_to(root)}"
            )
        seen[board.id] = board.manifest_path
        if board.project_dir in projects:
            errors.append(
                f"project_dir {board.project_dir} is registered by multiple boards; "
                "use a platform-project manifest only after it selects one board adapter"
            )
        projects[board.project_dir] = board.manifest_path
    if errors:
        raise ManifestError("\n".join(errors))
    return boards


def find_board(boards: Iterable[Board], board_id: str) -> Board:
    matches = [board for board in boards if board.id == board_id]
    if not matches:
        raise ManifestError(f"unknown board id: {board_id}")
    return matches[0]


def build_command(root: Path, board: Board, variant: str) -> tuple[list[str], Path]:
    if not board.has_variant(variant):
        raise ManifestError(f"{board.id}: unknown variant {variant}")
    project_dir = board.project_path(root)
    if board.data["platform"] == "esp-idf":
        return ["idf.py", "build"], project_dir
    if board.data["platform"] == "beken":
        return ["bash", "tools/build.sh"], project_dir
    raise ManifestError(f"unsupported platform: {board.data['platform']}")
