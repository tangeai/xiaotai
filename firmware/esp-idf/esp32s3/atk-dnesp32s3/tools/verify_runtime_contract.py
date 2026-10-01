#!/usr/bin/env python3
"""Verify the generated project's ThingConnect/TiRTC runtime protocol contract."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any


CALLBACKS = (
    "on_event",
    "on_conn_accepted",
    "on_conn_error",
    "on_disconnected",
    "on_ai_connect",
    "on_audio",
    "on_video",
    "on_command",
    "on_request_key_frame",
    "on_subscribe_audio",
    "on_subscribe_video",
    "on_unsubscribe_audio",
    "on_unsubscribe_video",
)
FORBIDDEN_CALLBACK_CALLS = (
    "TiRtcDisconnect(",
    "TiRtcStop(",
    "TiRtcUninit(",
)
VIDEO_PROFILES = {
    "mjpeg": ("TIRTC_VIDEO_JPEG", "complete_jpeg"),
    "h264": ("TIRTC_VIDEO_H264", "annex_b_access_unit"),
    "h265": ("TIRTC_VIDEO_H265", "annex_b_access_unit"),
}
BUSINESS_FEATURES = {"device_call", "wechat_voip"}
BUSINESS_PROTOCOL_TOKENS = {
    "device_call": (
        "/v1/call/request",
        "/v1/call/device/info",
        "/v1/call/reject",
        "/v1/call/cancel",
        "/v1/call/hangup",
        "/v1/call/room",
        "call_incoming",
        "room_cancel",
        "call_reject",
    ),
    "wechat_voip": (
        "/v1/voip/device/profile",
        "/v1/voip/device/contacts",
        "/v1/voip/device/call",
        "call_incoming",
        "callers_update",
        "wx_call_id",
        "wxcall",
    ),
}
GIT_SHA_RE = re.compile(r"^[0-9a-fA-F]{40}$")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def project_file(project: Path, value: Any, label: str) -> Path:
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"{label} must be a non-empty project-relative path")
    relative = Path(value)
    if relative.is_absolute():
        raise ValueError(f"{label} must be project-relative, got {value!r}")
    resolved = (project / relative).resolve()
    if project != resolved and project not in resolved.parents:
        raise ValueError(f"{label} escapes project root: {value!r}")
    return resolved


def function_body(text: str, name: str) -> str | None:
    pattern = re.compile(rf"\b{re.escape(name)}\s*\([^;]*?\)\s*\{{", re.DOTALL)
    match = pattern.search(text)
    if match is None:
        return None
    brace = text.find("{", match.start())
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[brace : index + 1]
    return None


def require_tokens(
    text: str, label: str, tokens: tuple[str, ...], errors: list[str]
) -> None:
    compact = re.sub(r"\s+", "", text)
    for token in tokens:
        if re.sub(r"\s+", "", token) not in compact:
            errors.append(f"{label} is missing protocol token: {token}")


def check_platform_contract(platform: Any, errors: list[str]) -> None:
    if not isinstance(platform, dict) or platform.get("schema_version") != 1:
        errors.append("platform media contract schema_version must be 1")
        return
    expected_audio = {
        ("h5", "audio_up"): (10, "alaw", 8000, 1),
        ("h5", "audio_down"): (14, "alaw", 8000, 1),
        ("ai", "audio_up"): (1, "alaw", 8000, 1),
        ("ai", "audio_down"): (1, "alaw", 8000, 1),
    }
    for (section, direction), expected in expected_audio.items():
        parent = platform.get(section)
        actual = parent.get(direction) if isinstance(parent, dict) else None
        if not isinstance(actual, dict):
            errors.append(f"platform contract is missing {section}.{direction}")
            continue
        values = (
            actual.get("stream_id"),
            actual.get("codec"),
            actual.get("sample_rate_hz"),
            actual.get("channels"),
        )
        if values != expected or actual.get("media") != "TIRTC_AUDIO_ALAW" or actual.get(
            "flags"
        ) != "TIRTC_AUDIOSAMPLE_8K16B1C":
            errors.append(
                f"platform {section}.{direction} must be stream/codec/rate/channels "
                f"{expected} with TiRTC A-law 8k flags"
            )
    h5 = platform.get("h5")
    video = h5.get("video_up") if isinstance(h5, dict) else None
    profiles = video.get("supported_profiles") if isinstance(video, dict) else None
    if not isinstance(video, dict) or video.get("stream_id") != 11 or not isinstance(
        profiles, list
    ):
        errors.append("platform contract must define H5 video stream 11 profiles")
    else:
        actual_profiles = {
            item.get("codec"): (item.get("media"), item.get("send_boundary"))
            for item in profiles
            if isinstance(item, dict) and isinstance(item.get("codec"), str)
        }
        if actual_profiles != VIDEO_PROFILES:
            errors.append(
                "platform H5 video profiles must declare MJPEG, H.264 and H.265 "
                "with their exact TiRTC media and send boundaries"
            )
    ai = platform.get("ai")
    response = ai.get("start_session_response") if isinstance(ai, dict) else None
    required = response.get("required_fields") if isinstance(response, dict) else None
    if (
        not isinstance(required, list)
        or not {
            "id",
            "result.session_id",
            "result.input_audio",
            "result.output_audio",
        }.issubset(set(required))
        or response.get("response_formats_authoritative") is not True
    ):
        errors.append(
            "platform AI contract must make session_id and response audio formats authoritative"
        )


def check_business_contract(
    contract: dict[str, Any],
    project: Path,
    errors: list[str],
    inputs: dict[str, str],
) -> set[str]:
    business = contract.get("business")
    if business is None:
        return set()
    if not isinstance(business, dict):
        errors.append("business must be an object")
        return set()
    features = business.get("features", [])
    if not isinstance(features, list) or any(
        not isinstance(feature, str) for feature in features
    ):
        errors.append("business.features must be an array of feature names")
        return set()
    selected = set(features)
    unknown = selected - BUSINESS_FEATURES
    if unknown:
        errors.append(
            "business.features contains unsupported features: "
            + ", ".join(sorted(unknown))
        )
    if not selected:
        return set()

    revision = business.get("protocol_revision")
    if not isinstance(revision, str) or not GIT_SHA_RE.fullmatch(revision):
        errors.append(
            "business.protocol_revision must pin the 40-character tirtc-server-example commit"
        )
    arbiter = business.get("session_arbiter")
    if not isinstance(arbiter, dict):
        errors.append("business.session_arbiter must be an object")
    else:
        for field in (
            "single_foreground_owner",
            "generation_guard",
            "monotonic_deadlines",
            "deferred_lifecycle",
            "restore_h5_after_call",
        ):
            if arbiter.get(field) is not True:
                errors.append(f"business.session_arbiter.{field} must be true")
        if arbiter.get("pending_capacity") != 1:
            errors.append("business.session_arbiter.pending_capacity must be 1")

    assertions = business.get("implementation_assertions")
    combined = ""
    if not isinstance(assertions, list) or not assertions:
        errors.append("business.implementation_assertions must be a non-empty array")
    else:
        for index, assertion in enumerate(assertions):
            label = f"business.implementation_assertions[{index}]"
            if not isinstance(assertion, dict):
                errors.append(f"{label} must be an object")
                continue
            try:
                source = project_file(project, assertion.get("file"), f"{label}.file")
            except ValueError as exc:
                errors.append(str(exc))
                continue
            if not source.is_file():
                errors.append(f"{label}.file does not exist: {source}")
                continue
            text = source.read_text(encoding="utf-8", errors="replace")
            combined += "\n" + text
            inputs[str(source.relative_to(project))] = sha256_file(source)
            for token in assertion.get("contains", []):
                if not isinstance(token, str) or not token:
                    errors.append(f"{label}.contains must contain non-empty strings")
                elif token not in text:
                    errors.append(f"{label} is missing token: {token}")

    for feature in sorted(selected & BUSINESS_FEATURES):
        for token in BUSINESS_PROTOCOL_TOKENS[feature]:
            if token not in combined:
                errors.append(f"business {feature} is missing protocol token: {token}")
    return selected & BUSINESS_FEATURES


def verify_contract(contract_path: Path, project_path: Path) -> dict[str, Any]:
    project = project_path.expanduser().resolve()
    contract_file = contract_path.expanduser().resolve()
    errors: list[str] = []
    inputs: dict[str, str] = {}
    if not project.is_dir():
        return {"ok": False, "errors": [f"project directory does not exist: {project}"]}
    if not contract_file.is_file():
        return {"ok": False, "errors": [f"runtime contract does not exist: {contract_file}"]}
    if project != contract_file and project not in contract_file.parents:
        return {"ok": False, "errors": ["runtime contract must be inside the project"]}
    try:
        contract = json.loads(contract_file.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return {"ok": False, "errors": [f"invalid runtime contract: {exc}"]}
    if not isinstance(contract, dict) or contract.get("schema_version") != 1:
        return {"ok": False, "errors": ["runtime contract schema_version must be 1"]}
    inputs[str(contract_file.relative_to(project))] = sha256_file(contract_file)

    files = contract.get("files")
    if not isinstance(files, dict):
        return {"ok": False, "errors": ["runtime contract files must be an object"]}
    contents: dict[str, str] = {}
    for name in ("platform_client", "app_main", "starter_tirtc", "starter_runtime"):
        try:
            path = project_file(project, files.get(name), f"files.{name}")
        except ValueError as exc:
            errors.append(str(exc))
            continue
        if not path.is_file():
            errors.append(f"files.{name} does not exist: {path}")
            continue
        contents[name] = path.read_text(encoding="utf-8", errors="replace")
        inputs[str(path.relative_to(project))] = sha256_file(path)

    try:
        platform_path = project_file(
            project, contract.get("platform_contract"), "platform_contract"
        )
    except ValueError as exc:
        errors.append(str(exc))
        platform_path = None
    if platform_path is not None:
        if not platform_path.is_file():
            errors.append(f"platform media contract does not exist: {platform_path}")
        else:
            inputs[str(platform_path.relative_to(project))] = sha256_file(platform_path)
            try:
                check_platform_contract(
                    json.loads(platform_path.read_text(encoding="utf-8")), errors
                )
            except json.JSONDecodeError as exc:
                errors.append(f"invalid platform media contract: {exc}")

    platform_source = contents.get("platform_client", "")
    app_main = contents.get("app_main", "")
    tirtc = contents.get("starter_tirtc", "")
    runtime = contents.get("starter_runtime", "")
    require_tokens(
        platform_source,
        "platform_client",
        ('"tirtc-srv"', "platform_client_tirtc_endpoint", "s_services.tirtc"),
        errors,
    )
    require_tokens(
        app_main,
        "app_main",
        ("platform_client_tirtc_endpoint()", ".service_endpoint = tirtc_endpoint"),
        errors,
    )
    require_tokens(
        tirtc,
        "starter_tirtc",
        (
            "TIRTC_OPT_SERVICE_ENDPOINT",
            "config->service_endpoint",
            ".stream_id = mode == STARTER_TIRTC_H5 ? H5_AUDIO_STREAM : AI_AUDIO_STREAM",
            ".media = TIRTC_AUDIO_ALAW",
            ".flags = TIRTC_AUDIOSAMPLE_8K16B1C",
            "frame->media != TIRTC_AUDIO_ALAW",
            "frame->flags != TIRTC_AUDIOSAMPLE_8K16B1C",
            "TIRTC_VIDEO_JPEG",
            "TIRTC_VIDEO_H264",
            "TIRTC_VIDEO_H265",
            "defer_disconnect(connection)",
        ),
        errors,
    )
    for callback in CALLBACKS:
        body = function_body(tirtc, callback)
        if body is None:
            errors.append(f"starter_tirtc callback {callback} is missing")
            continue
        for forbidden in FORBIDDEN_CALLBACK_CALLS:
            if forbidden in body:
                errors.append(
                    f"starter_tirtc callback {callback} calls forbidden lifecycle API {forbidden[:-1]}"
                )
    worker = function_body(tirtc, "deferred_disconnect_task")
    if worker is None or "TiRtcDisconnect(" not in worker:
        errors.append("deferred disconnect worker must own TiRtcDisconnect")

    require_tokens(
        runtime,
        "starter_runtime",
        (
            '"input_audio"',
            '"output_audio"',
            '"codec", "alaw"',
            '"sample_rate", 8000',
            '"channels", 1',
            '"session_id"',
            "ai_audio_format_is_alaw_8k_mono(input_audio)",
            "ai_audio_format_is_alaw_8k_mono(output_audio)",
            "has_result && !accepted",
            'strcmp(method->valuestring, "end_session")',
        ),
        errors,
    )
    ai_handler = function_body(runtime, "handle_ai_command")
    if ai_handler is None or "starter_media_start(STARTER_TIRTC_AI" not in re.sub(
        r"\s+", "", ai_handler
    ):
        errors.append("AI media must start only from the validated command handler")

    base_errors = list(errors)
    business_errors: list[str] = []
    business_features = check_business_contract(
        contract, project, business_errors, inputs
    )
    errors.extend(business_errors)

    return {
        "ok": not errors,
        "base_ok": not base_errors,
        "business_ok": not business_errors,
        "summary": (
            "endpoint + callbacks + H5/AI stream and negotiation contract; "
            f"business={','.join(sorted(business_features)) or 'none'}"
        ),
        "business_features": sorted(business_features),
        "business_errors": business_errors,
        "inputs": inputs,
        "errors": errors,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Verify TiRTC endpoint, callbacks, stream metadata, and AI negotiation."
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
        output.write_text(
            json.dumps(result, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=2))
    elif result["ok"]:
        print(f"PASS: TiRTC runtime contract: {result['summary']}")
    else:
        for error in result["errors"]:
            print(f"FAIL: {error}", file=sys.stderr)
    return 0 if result["ok"] else 3


if __name__ == "__main__":
    raise SystemExit(main())
