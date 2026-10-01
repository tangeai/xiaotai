#!/usr/bin/env python3
"""Validate the small set of source-controlled product assets."""

from __future__ import annotations

import hashlib
from pathlib import Path
import wave

from repository import find_repository_root


PROJECT = Path(__file__).resolve().parents[1]
REPO = find_repository_root(PROJECT)

WAV_ASSETS = {
    "rings/kids_watch_incoming_tiny.wav": (8000, 1, 2),
    "rings/kids_watch_outgoing_tiny.wav": (8000, 1, 2),
    "prompts/ai_ack_female_8k.wav": (8000, 1, 2),
    "prompts/ai_ack_male_8k.wav": (8000, 1, 2),
}

FORBIDDEN = (
    REPO / "docs/rings",
    REPO / "docs/xiao-tai-esp32s3-multinet7",
    PROJECT / "components/starter_voice/assets/multinet7",
)

WECHAT_QR_SHA256 = "18340f1a9ae08ae4b654962c8b5f00b3e1bc4eaa9e04417b9f4988d653e39266"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    for relative, expected in WAV_ASSETS.items():
        path = REPO / "product/assets/audio" / relative
        assert path.is_file(), f"missing product audio: {relative}"
        with wave.open(str(path), "rb") as wav:
            actual = (wav.getframerate(), wav.getnchannels(), wav.getsampwidth())
        assert actual == expected, f"unexpected WAV format for {relative}: {actual}"
        assert path.read_bytes()[36:40] == b"data", f"non-canonical WAV header: {relative}"

    source_dir = REPO / "product/assets/models/nihaoxiaotai/nihaoxiaotai_v9.3_voice_r2_tflite"
    source_model = source_dir / "nihaoxiaotai.tflite"
    source_head = source_dir / "head.h"
    targets = (
        PROJECT / "components/starter_voice/model",
        REPO / "firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/components/starter_voice/model",
    )
    for path in (source_model, source_head):
        assert path.is_file(), f"missing wake asset: {path.relative_to(REPO)}"
    for target in targets:
        build_model = target / "nihaoxiaotai.tflite"
        build_head = target / "head.h"
        for path in (build_model, build_head):
            assert path.is_file(), f"missing wake asset: {path.relative_to(REPO)}"
        assert digest(source_model) == digest(build_model), \
            f"wake model source/build copies differ: {target.relative_to(REPO)}"
        assert digest(source_head) == digest(build_head), \
            f"wake head source/build copies differ: {target.relative_to(REPO)}"

    qr = REPO / "product/assets/qr/wx_xiaotai.png"
    assert qr.is_file(), f"missing WeChat QR asset: {qr.relative_to(REPO)}"
    assert digest(qr) == WECHAT_QR_SHA256, \
        f"unexpected WeChat QR asset: {qr.relative_to(REPO)}"

    for path in FORBIDDEN:
        assert not path.exists(), f"obsolete or duplicate asset returned: {path.relative_to(REPO)}"

    cmake = (REPO / "platforms/esp-idf/components/starter_product/CMakeLists.txt").read_text()
    for relative in WAV_ASSETS:
        assert f'"{relative}"' in cmake, f"audio is not embedded: {relative}"
    voice_cmake = (PROJECT / "components/starter_voice/CMakeLists.txt").read_text()
    common_voice_cmake = (REPO / "platforms/esp-idf/components/starter_voice/CMakeLists.txt").read_text()
    assert 'XIAOTAI_STARTER_VOICE_MODEL_DIR' in voice_cmake
    assert 'target_add_binary_data' in common_voice_cmake
    assert 'RENAME_TO "nihaoxiaotai.tflite"' in common_voice_cmake

    print("release asset policy: PASS")


if __name__ == "__main__":
    main()
