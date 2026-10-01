#!/usr/bin/env python3
"""Verify a project-local TiRTC video pipeline contract."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any


VIDEO_MEDIA = {
    "mjpeg": "TIRTC_VIDEO_JPEG",
    "h264": "TIRTC_VIDEO_H264",
    "h265": "TIRTC_VIDEO_H265",
}
VIDEO_BOUNDARY = {
    "mjpeg": "complete_jpeg",
    "h264": "annex_b_access_unit",
    "h265": "annex_b_access_unit",
}
REPOSITORY_MARKER = Path("boards/schema/board.schema.json")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def repository_root(project: Path) -> Path:
    project = project.resolve()
    for candidate in (project, *project.parents):
        if (candidate / REPOSITORY_MARKER).is_file():
            return candidate
    raise ValueError(f"cannot locate repository root from project: {project}")


def project_file(project: Path, value: Any, label: str) -> Path:
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"{label} must be a non-empty project-relative path")
    if value.startswith("@repo/"):
        repo = repository_root(project)
        resolved = (repo / value.removeprefix("@repo/")).resolve()
        if repo != resolved and repo not in resolved.parents:
            raise ValueError(f"{label} escapes repository root: {value!r}")
        return resolved
    relative = Path(value)
    if relative.is_absolute():
        raise ValueError(f"{label} must be project-relative, got {value!r}")
    resolved = (project / relative).resolve()
    if project != resolved and project not in resolved.parents:
        raise ValueError(f"{label} escapes project root: {value!r}")
    return resolved


def input_name(project: Path, source: Path) -> str:
    if project in source.parents:
        return str(source.relative_to(project))
    return "@repo/" + str(source.relative_to(repository_root(project)))


def mapping(value: Any, label: str, errors: list[str]) -> dict[str, Any]:
    if not isinstance(value, dict):
        errors.append(f"{label} must be an object")
        return {}
    return value


def positive_int(value: Any, label: str, errors: list[str]) -> int | None:
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        errors.append(f"{label} must be a positive integer")
        return None
    return value


def lock_versions(lock_text: str) -> dict[str, str]:
    versions: dict[str, str] = {}
    current: str | None = None
    for line in lock_text.splitlines():
        match = re.match(r"^  ([^ ].*):$", line)
        if match:
            current = match.group(1)
            continue
        if current is not None:
            version = re.match(r"^    version:\s*['\"]?([^'\"\s]+)", line)
            if version:
                versions[current] = version.group(1)
                current = None
    return versions


def check_dependencies(
    project: Path,
    contract: dict[str, Any],
    errors: list[str],
    inputs: dict[str, str],
) -> None:
    dependencies = contract.get("dependencies")
    if not isinstance(dependencies, dict) or not dependencies:
        errors.append("dependencies must map component names to exact locked versions")
        return
    lock = project / "dependencies.lock"
    if not lock.is_file():
        errors.append("dependencies.lock is missing")
        return
    text = lock.read_text(encoding="utf-8", errors="replace")
    inputs["dependencies.lock"] = sha256_file(lock)
    locked = lock_versions(text)
    for name, expected in dependencies.items():
        if not isinstance(name, str) or not isinstance(expected, str) or not expected:
            errors.append("dependencies entries must be non-empty string pairs")
        elif locked.get(name) != expected:
            errors.append(
                f"locked dependency mismatch for {name}: "
                f"expected {expected}, got {locked.get(name, 'missing')}"
            )


def check_platform_contract(
    project: Path,
    contract: dict[str, Any],
    errors: list[str],
    inputs: dict[str, str],
) -> None:
    try:
        path = project_file(
            project, contract.get("platform_contract"), "platform_contract"
        )
    except ValueError as exc:
        errors.append(str(exc))
        return
    if not path.is_file():
        errors.append(f"platform media contract does not exist: {path}")
        return
    try:
        platform = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        errors.append(f"invalid platform media contract: {exc}")
        return
    inputs[input_name(project, path)] = sha256_file(path)
    if not isinstance(platform, dict) or platform.get("schema_version") != 1:
        errors.append("platform media contract schema_version must be 1")
        return
    h5 = platform.get("h5")
    video = h5.get("video_up") if isinstance(h5, dict) else None
    if not isinstance(video, dict) or video.get("stream_id") != 11:
        errors.append("platform media contract must define H5 video_up stream 11")
        return
    profiles = video.get("supported_profiles")
    if not isinstance(profiles, list):
        errors.append("platform H5 video supported_profiles must be an array")
        return
    supported: dict[str, dict[str, Any]] = {}
    for index, item in enumerate(profiles):
        if not isinstance(item, dict):
            errors.append(
                f"platform supported_profiles[{index}] must be an object"
            )
            continue
        codec = item.get("codec")
        if codec not in VIDEO_MEDIA:
            errors.append(
                f"platform supported_profiles[{index}].codec is unsupported"
            )
            continue
        if codec in supported:
            errors.append(f"platform video profile {codec} is duplicated")
            continue
        if item.get("media") != VIDEO_MEDIA[codec]:
            errors.append(
                f"platform video profile {codec} media must be {VIDEO_MEDIA[codec]}"
            )
        if item.get("send_boundary") != VIDEO_BOUNDARY[codec]:
            errors.append(
                f"platform video profile {codec} boundary must be "
                f"{VIDEO_BOUNDARY[codec]}"
            )
        supported[codec] = item
    camera = contract.get("camera")
    selected = camera.get("codec") if isinstance(camera, dict) else None
    if selected in VIDEO_MEDIA and selected not in supported:
        errors.append(
            f"board-selected codec {selected} is not supported by platform contract"
        )


def check_scheduler(
    project: Path,
    contract: dict[str, Any],
    errors: list[str],
    inputs: dict[str, str],
) -> None:
    scheduler = mapping(contract.get("scheduler"), "scheduler", errors)
    wifi_core = scheduler.get("wifi_core")
    camera_core = scheduler.get("camera_core")
    for value, label in ((wifi_core, "wifi_core"), (camera_core, "camera_core")):
        if isinstance(value, bool) or not isinstance(value, int) or value < 0:
            errors.append(f"scheduler.{label} must be an integer >= 0")
    if isinstance(wifi_core, int) and isinstance(camera_core, int) and wifi_core == camera_core:
        errors.append("camera event processing must not share the configured Wi-Fi core")
    config_files = scheduler.get("config_files")
    if not isinstance(config_files, list) or not config_files:
        errors.append("scheduler.config_files must be a non-empty array")
        return
    for index, value in enumerate(config_files):
        try:
            config = project_file(project, value, f"scheduler.config_files[{index}]")
        except ValueError as exc:
            errors.append(str(exc))
            continue
        if not config.is_file():
            errors.append(f"scheduler config does not exist: {config}")
            continue
        text = config.read_text(encoding="utf-8", errors="replace")
        inputs[input_name(project, config)] = sha256_file(config)
        if isinstance(camera_core, int) and f"CONFIG_CAMERA_CORE{camera_core}=y" not in text:
            errors.append(f"{config.relative_to(project)} does not select camera core {camera_core}")
        if isinstance(wifi_core, int) and f"CONFIG_CAMERA_CORE{wifi_core}=y" in text:
            errors.append(f"{config.relative_to(project)} pins camera processing to Wi-Fi core {wifi_core}")
    wifi_config_files = scheduler.get("wifi_config_files")
    if not isinstance(wifi_config_files, list) or not wifi_config_files:
        errors.append("scheduler.wifi_config_files must be a non-empty array")
        return
    for index, value in enumerate(wifi_config_files):
        try:
            config = project_file(
                project, value, f"scheduler.wifi_config_files[{index}]"
            )
        except ValueError as exc:
            errors.append(str(exc))
            continue
        if not config.is_file():
            errors.append(f"Wi-Fi scheduler config does not exist: {config}")
            continue
        text = config.read_text(encoding="utf-8", errors="replace")
        inputs[input_name(project, config)] = sha256_file(config)
        if (
            isinstance(wifi_core, int)
            and f"CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_{wifi_core}=y" not in text
        ):
            errors.append(
                f"{config.relative_to(project)} does not pin Wi-Fi task to core {wifi_core}"
            )


def check_pipeline(contract: dict[str, Any], errors: list[str]) -> None:
    camera = mapping(contract.get("camera"), "camera", errors)
    codec = camera.get("codec")
    if codec not in VIDEO_MEDIA:
        errors.append("camera.codec must be one of h264, h265, mjpeg")
    else:
        boundary_field = (
            "complete_jpeg_per_send"
            if codec == "mjpeg"
            else "complete_access_unit_per_send"
        )
        if camera.get(boundary_field) is not True:
            errors.append(f"camera.{boundary_field} must be true")
        expected_media = VIDEO_MEDIA[codec]
        if camera.get("media") != expected_media:
            errors.append(f"camera.media must be {expected_media} for {codec}")
    if camera.get("stream_id") != 11:
        errors.append("camera.stream_id must be 11 for the H5 video contract")
    frame_buffers = positive_int(camera.get("frame_buffers"), "camera.frame_buffers", errors)
    if frame_buffers is not None and frame_buffers < 2:
        errors.append("camera.frame_buffers must be at least 2 for the selected realtime pipeline")
    accepted_pids = camera.get("accepted_sensor_pids")
    if (
        not isinstance(accepted_pids, list)
        or not accepted_pids
        or any(
            isinstance(pid, bool)
            or not isinstance(pid, int)
            or pid < 0
            or pid > 0xFFFF
            for pid in accepted_pids
        )
        or len(set(accepted_pids)) != len(accepted_pids)
    ):
        errors.append(
            "camera.accepted_sensor_pids must be a non-empty unique uint16 array"
        )
    if camera.get("unknown_sensor_policy") != "reject":
        errors.append("camera.unknown_sensor_policy must be reject")

    memory = mapping(contract.get("memory"), "memory", errors)
    send_buffer = positive_int(
        memory.get("max_send_buffer_bytes"), "memory.max_send_buffer_bytes", errors
    )
    frame_field = (
        "max_complete_jpeg_bytes"
        if codec == "mjpeg"
        else "max_complete_access_unit_bytes"
    )
    complete_frame = positive_int(
        memory.get(frame_field), f"memory.{frame_field}", errors
    )
    backpressure = positive_int(
        memory.get("video_backpressure_bytes"), "memory.video_backpressure_bytes", errors
    )
    if None not in (send_buffer, complete_frame, backpressure):
        if complete_frame > backpressure:
            errors.append(
                "one complete encoded frame/access unit must fit below the "
                "video backpressure threshold"
            )
        if backpressure >= send_buffer:
            errors.append("video backpressure threshold must be lower than max send buffer")


def check_assertions(
    project: Path,
    contract: dict[str, Any],
    errors: list[str],
    inputs: dict[str, str],
) -> None:
    assertions = contract.get("implementation_assertions")
    if not isinstance(assertions, list) or not assertions:
        errors.append("implementation_assertions must be a non-empty array")
        return
    for index, item in enumerate(assertions):
        label = f"implementation_assertions[{index}]"
        assertion = mapping(item, label, errors)
        try:
            source = project_file(project, assertion.get("file"), f"{label}.file")
        except ValueError as exc:
            errors.append(str(exc))
            continue
        if not source.is_file():
            errors.append(f"{label}.file does not exist: {source}")
            continue
        text = source.read_text(encoding="utf-8", errors="replace")
        compact = re.sub(r"\s+", "", text)
        inputs[input_name(project, source)] = sha256_file(source)
        for field, haystack, should_exist in (
            ("contains", text, True),
            ("contains_compact", compact, True),
            ("absent", text, False),
            ("absent_compact", compact, False),
        ):
            needles = assertion.get(field, [])
            if not isinstance(needles, list) or any(
                not isinstance(needle, str) or not needle for needle in needles
            ):
                errors.append(f"{label}.{field} must be an array of non-empty strings")
                continue
            for needle in needles:
                found = needle in haystack
                if found != should_exist:
                    verb = "missing" if should_exist else "contains forbidden"
                    errors.append(f"{label} {verb} {field} token: {needle}")


def verify_contract(contract_path: Path, project_path: Path) -> dict[str, Any]:
    project = project_path.expanduser().resolve()
    contract_file = contract_path.expanduser().resolve()
    if not project.is_dir():
        return {"ok": False, "errors": [f"project directory does not exist: {project}"]}
    if not contract_file.is_file():
        return {"ok": False, "errors": [f"video contract does not exist: {contract_file}"]}
    if project != contract_file and project not in contract_file.parents:
        return {"ok": False, "errors": ["video contract must be inside the project"]}
    try:
        contract = json.loads(contract_file.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return {"ok": False, "errors": [f"invalid video contract: {exc}"]}
    if not isinstance(contract, dict):
        return {"ok": False, "errors": ["video contract root must be an object"]}
    errors: list[str] = []
    inputs = {str(contract_file.relative_to(project)): sha256_file(contract_file)}
    if contract.get("schema_version") != 1:
        errors.append("schema_version must be 1")
    evidence = contract.get("evidence")
    if not isinstance(evidence, list) or len(evidence) < 2 or any(
        not isinstance(item, str) or not item for item in evidence
    ):
        errors.append("evidence must contain at least two non-empty source IDs")
    check_dependencies(project, contract, errors, inputs)
    check_platform_contract(project, contract, errors, inputs)
    check_scheduler(project, contract, errors, inputs)
    check_pipeline(contract, errors)
    check_assertions(project, contract, errors, inputs)
    camera = contract.get("camera", {})
    memory = contract.get("memory", {})
    return {
        "ok": not errors,
        "summary": (
            f"codec={camera.get('codec')} stream={camera.get('stream_id')} "
            f"buffers={camera.get('frame_buffers')} "
            f"send_buffer={memory.get('max_send_buffer_bytes')}"
        ),
        "inputs": inputs,
        "errors": errors,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Verify locked camera dependencies, scheduler isolation, JPEG framing, memory, and adapter assertions."
    )
    parser.add_argument("contract", type=Path)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--evidence-out", type=Path)
    args = parser.parse_args()
    result = verify_contract(args.contract, args.project)
    if args.evidence_out is not None:
        output = args.evidence_out.expanduser().resolve()
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=2))
    elif result["ok"]:
        print(f"PASS: video semantic contract: {result['summary']}")
    else:
        for error in result["errors"]:
            print(f"FAIL: {error}", file=sys.stderr)
    return 0 if result["ok"] else 3


if __name__ == "__main__":
    raise SystemExit(main())
