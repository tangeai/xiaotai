#!/usr/bin/env python3
"""Verify an ESP32 board audio contract against locked source and adapter files."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any


PAIR_PATTERN = re.compile(r"\{\s*(\d+)\s*,\s*(\d+)\s*,")
AUDIO_MODES = {"standard", "tdm", "dsp", "pcm"}
HANDOFF_POLICIES = {"none", "release_before_claim", "delete_recreate"}
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


def require_mapping(value: Any, label: str, errors: list[str]) -> dict[str, Any]:
    if not isinstance(value, dict):
        errors.append(f"{label} must be an object")
        return {}
    return value


def require_positive_int(value: Any, label: str, errors: list[str]) -> int | None:
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        errors.append(f"{label} must be a positive integer")
        return None
    return value


def require_nonempty_string(value: Any, label: str, errors: list[str]) -> str | None:
    if not isinstance(value, str) or not value.strip():
        errors.append(f"{label} must be a non-empty string")
        return None
    return value


def check_clock(
    project: Path,
    contract: dict[str, Any],
    errors: list[str],
    inputs: dict[str, str],
) -> None:
    clock = require_mapping(contract.get("clock"), "clock", errors)
    sample_rate = require_positive_int(
        clock.get("sample_rate_hz"), "clock.sample_rate_hz", errors
    )
    ratio = require_positive_int(clock.get("mclk_ratio"), "clock.mclk_ratio", errors)
    mclk = require_positive_int(clock.get("mclk_hz"), "clock.mclk_hz", errors)
    if None not in (sample_rate, ratio, mclk) and sample_rate * ratio != mclk:
        errors.append(
            "clock tuple is inconsistent: "
            f"{sample_rate}Hz x {ratio} != {mclk}Hz"
        )

    tables = clock.get("codec_tables")
    if not isinstance(tables, list) or not tables:
        errors.append("clock.codec_tables must list every clocked codec driver table")
        return
    if sample_rate is None or mclk is None:
        return

    for index, item in enumerate(tables):
        label = f"clock.codec_tables[{index}]"
        table = require_mapping(item, label, errors)
        codec = require_nonempty_string(table.get("codec"), f"{label}.codec", errors)
        try:
            source = project_file(project, table.get("source"), f"{label}.source")
        except ValueError as exc:
            errors.append(str(exc))
            continue
        if not source.is_file():
            errors.append(f"{label}.source does not exist: {source}")
            continue
        text = source.read_text(encoding="utf-8", errors="replace")
        pairs = {(int(first), int(second)) for first, second in PAIR_PATTERN.findall(text)}
        inputs[input_name(project, source)] = sha256_file(source)
        if not pairs:
            errors.append(f"{label} contains no C initializer clock pairs")
        elif (mclk, sample_rate) not in pairs:
            errors.append(
                f"{codec or label} rejects selected clock tuple: "
                f"sample_rate={sample_rate}Hz mclk={mclk}Hz"
            )


def check_endpoint(
    value: Any,
    label: str,
    errors: list[str],
) -> dict[str, Any]:
    endpoint = require_mapping(value, label, errors)
    controller = endpoint.get("controller")
    if isinstance(controller, bool) or not isinstance(controller, int) or controller < 0:
        errors.append(f"{label}.controller must be an integer >= 0")
    mode = endpoint.get("mode")
    if mode not in AUDIO_MODES:
        errors.append(f"{label}.mode must be one of {', '.join(sorted(AUDIO_MODES))}")
    role = endpoint.get("role")
    if role not in {"master", "slave"}:
        errors.append(f"{label}.role must be master or slave")
    return endpoint


def check_topology(contract: dict[str, Any], errors: list[str]) -> None:
    capture = check_endpoint(contract.get("capture"), "capture", errors)
    playback = check_endpoint(contract.get("playback"), "playback", errors)

    capture_mode = capture.get("mode")
    tdm_enabled = capture.get("tdm_enabled")
    if not isinstance(tdm_enabled, bool):
        errors.append("capture.tdm_enabled must be true or false")
    elif (capture_mode == "tdm") != tdm_enabled:
        errors.append("capture.mode and capture.tdm_enabled disagree")

    if capture_mode == "tdm":
        slot_count = require_positive_int(
            capture.get("slot_count"), "capture.slot_count", errors
        )
        slot_order = capture.get("slot_order")
        slot_signals = capture.get("slot_signals")
        selected_slot = capture.get("selected_slot")
        selected_signal = capture.get("selected_signal")
        if not isinstance(slot_order, list) or not slot_order:
            errors.append("capture.slot_order must be a non-empty array in TDM mode")
        if not isinstance(slot_signals, list) or not slot_signals:
            errors.append("capture.slot_signals must be a non-empty array in TDM mode")
        if slot_count is not None:
            if isinstance(slot_order, list) and len(slot_order) != slot_count:
                errors.append("capture.slot_order length must equal capture.slot_count")
            if isinstance(slot_signals, list) and len(slot_signals) != slot_count:
                errors.append("capture.slot_signals length must equal capture.slot_count")
        if (
            isinstance(selected_slot, bool)
            or not isinstance(selected_slot, int)
            or selected_slot < 0
            or slot_count is None
            or selected_slot >= slot_count
        ):
            errors.append("capture.selected_slot must select an available TDM slot")
        elif isinstance(slot_signals, list) and selected_slot < len(slot_signals):
            if selected_signal != slot_signals[selected_slot]:
                errors.append(
                    "capture.selected_signal does not match capture.slot_signals at "
                    "capture.selected_slot"
                )
        require_nonempty_string(
            capture.get("mapping_evidence"), "capture.mapping_evidence", errors
        )
    else:
        require_nonempty_string(
            capture.get("mapping_evidence"), "capture.mapping_evidence", errors
        )

    shared = require_mapping(contract.get("shared_clock"), "shared_clock", errors)
    gpios = shared.get("gpios")
    if not isinstance(gpios, list) or not gpios or any(
        isinstance(gpio, bool) or not isinstance(gpio, int) or gpio < 0 for gpio in gpios
    ):
        errors.append("shared_clock.gpios must be a non-empty array of GPIO numbers")
    simultaneous = shared.get("directions_simultaneous")
    if not isinstance(simultaneous, bool):
        errors.append("shared_clock.directions_simultaneous must be true or false")
    handoff = shared.get("handoff")
    if handoff not in HANDOFF_POLICIES:
        errors.append(
            "shared_clock.handoff must be one of "
            + ", ".join(sorted(HANDOFF_POLICIES))
        )
    if simultaneous is False and handoff == "none":
        errors.append("half-duplex shared clocks require an explicit handoff")
    if simultaneous is True and handoff != "none":
        errors.append("simultaneous directions must not use a clock handoff")
    paired_channels = shared.get("paired_channels", False)
    if not isinstance(paired_channels, bool):
        errors.append("shared_clock.paired_channels must be true or false")
        paired_channels = False

    mixed_paired_ok = False
    if simultaneous is True and capture.get("mode") != playback.get("mode"):
        if paired_channels is not True:
            errors.append(
                "simultaneous mixed I2S modes require shared_clock.paired_channels=true"
            )
        if capture.get("controller") != playback.get("controller"):
            errors.append(
                "simultaneous mixed I2S modes require paired directions on one controller"
            )
        frame_bits: dict[str, int] = {}
        for endpoint, label in ((capture, "capture"), (playback, "playback")):
            slots = require_positive_int(
                endpoint.get("slot_count"), f"{label}.slot_count", errors
            )
            width = require_positive_int(
                endpoint.get("slot_bit_width"),
                f"{label}.slot_bit_width",
                errors,
            )
            if slots is not None and width is not None:
                frame_bits[label] = slots * width
        if len(frame_bits) == 2 and frame_bits["capture"] != frame_bits["playback"]:
            errors.append(
                "simultaneous mixed I2S modes require equal BCLKs per frame: "
                f"capture={frame_bits['capture']} playback={frame_bits['playback']}"
            )
        mixed_paired_ok = (
            paired_channels is True
            and capture.get("controller") == playback.get("controller")
            and len(frame_bits) == 2
            and frame_bits["capture"] == frame_bits["playback"]
        )
    if (
        capture.get("controller") == playback.get("controller")
        and capture.get("mode") != playback.get("mode")
        and not mixed_paired_ok
        and handoff != "delete_recreate"
    ):
        errors.append(
            "one I2S controller cannot retain different capture/playback modes; "
            "use paired frame-compatible directions, distinct controllers, or "
            "delete_recreate handoff"
        )

    aec_value = contract.get("echo_cancellation")
    if aec_value is None:
        return
    aec = require_mapping(aec_value, "echo_cancellation", errors)
    enabled = aec.get("enabled")
    if not isinstance(enabled, bool):
        errors.append("echo_cancellation.enabled must be true or false")
        return
    if not enabled:
        return
    if simultaneous is not True:
        errors.append("echo cancellation requires simultaneous capture and playback")
    if capture_mode != "tdm":
        errors.append("echo cancellation reference mapping requires TDM capture")
    sample_rate = require_positive_int(
        aec.get("sample_rate_hz"), "echo_cancellation.sample_rate_hz", errors
    )
    clock = contract.get("clock", {})
    if sample_rate is not None and sample_rate != clock.get("sample_rate_hz"):
        errors.append("echo cancellation sample rate must match the hardware clock")

    microphone_slot = aec.get("microphone_slot")
    reference_slot = aec.get("reference_slot")
    microphone_signal = require_nonempty_string(
        aec.get("microphone_signal"), "echo_cancellation.microphone_signal", errors
    )
    reference_signal = require_nonempty_string(
        aec.get("reference_signal"), "echo_cancellation.reference_signal", errors
    )
    slot_signals = capture.get("slot_signals")
    slot_count = capture.get("slot_count")
    for slot, signal, label in (
        (microphone_slot, microphone_signal, "microphone"),
        (reference_slot, reference_signal, "reference"),
    ):
        if (
            isinstance(slot, bool)
            or not isinstance(slot, int)
            or slot < 0
            or not isinstance(slot_count, int)
            or slot >= slot_count
        ):
            errors.append(
                f"echo_cancellation.{label}_slot must select an available TDM slot"
            )
        elif isinstance(slot_signals, list) and slot < len(slot_signals):
            if signal != slot_signals[slot]:
                errors.append(
                    f"echo_cancellation.{label}_signal does not match "
                    f"capture.slot_signals at slot {slot}"
                )
    if microphone_slot == reference_slot:
        errors.append("echo cancellation microphone and reference slots must differ")
    if microphone_signal is not None and microphone_signal == reference_signal:
        errors.append("echo cancellation microphone and reference signals must differ")


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
        assertion = require_mapping(item, label, errors)
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
        for field, haystack, present in (
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
                if found != present:
                    verb = "missing" if present else "contains forbidden"
                    errors.append(f"{label} {verb} {field} token: {needle}")


def verify_contract(contract_path: Path, project_path: Path) -> dict[str, Any]:
    project = project_path.expanduser().resolve()
    contract_file = contract_path.expanduser().resolve()
    errors: list[str] = []
    inputs: dict[str, str] = {}
    if not project.is_dir():
        return {"ok": False, "errors": [f"project directory does not exist: {project}"]}
    if not contract_file.is_file():
        return {"ok": False, "errors": [f"audio contract does not exist: {contract_file}"]}
    if project != contract_file and project not in contract_file.parents:
        return {"ok": False, "errors": ["audio contract must be inside the project"]}
    try:
        contract = json.loads(contract_file.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return {"ok": False, "errors": [f"invalid audio contract: {exc}"]}
    if not isinstance(contract, dict):
        return {"ok": False, "errors": ["audio contract root must be an object"]}
    if contract.get("schema_version") != 1:
        errors.append("schema_version must be 1")
    evidence = contract.get("evidence")
    if not isinstance(evidence, list) or len(evidence) < 2 or any(
        not isinstance(item, str) or not item for item in evidence
    ):
        errors.append("evidence must contain at least two non-empty source IDs")
    inputs[str(contract_file.relative_to(project))] = sha256_file(contract_file)
    check_clock(project, contract, errors, inputs)
    check_topology(contract, errors)
    check_assertions(project, contract, errors, inputs)
    clock = contract.get("clock", {})
    shared = contract.get("shared_clock", {})
    aec = contract.get("echo_cancellation", {})
    return {
        "ok": not errors,
        "summary": (
            f"sample_rate={clock.get('sample_rate_hz')}Hz "
            f"mclk={clock.get('mclk_hz')}Hz ratio={clock.get('mclk_ratio')} "
            f"simultaneous={shared.get('directions_simultaneous')} "
            f"aec={aec.get('enabled', False)}"
        ),
        "simultaneous_capture_playback": shared.get("directions_simultaneous") is True,
        "echo_cancellation_enabled": aec.get("enabled") is True,
        "inputs": inputs,
        "errors": errors,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Verify codec clocks, I2S topology, channel mapping, and adapter assertions."
    )
    parser.add_argument("contract", type=Path)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--evidence-out", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    result = verify_contract(args.contract, args.project)
    if args.evidence_out is not None:
        output = args.evidence_out.expanduser().resolve()
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=2))
    elif result["ok"]:
        print(f"PASS: audio semantic contract: {result['summary']}")
    else:
        for error in result["errors"]:
            print(f"FAIL: {error}", file=sys.stderr)
    return 0 if result["ok"] else 3


if __name__ == "__main__":
    raise SystemExit(main())
