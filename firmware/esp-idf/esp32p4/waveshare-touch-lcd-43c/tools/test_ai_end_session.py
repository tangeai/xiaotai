"""Regression contract for graceful AI server-side session shutdown."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[5]
RUNTIME = ROOT / "platforms/esp-idf/components/starter_runtime/src/starter_runtime.c"
RUNTIME_CMAKE = ROOT / "platforms/esp-idf/components/starter_runtime/CMakeLists.txt"
MEDIA_HEADER = ROOT / "platforms/esp-idf/components/starter_media_common/include/starter_media.h"
P4_MEDIA = (
    ROOT
    / "firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/components/starter_media/src/starter_media.c"
)


class AiEndSessionContractTest(unittest.TestCase):
    def test_shared_runtime_uses_tested_end_drain_policy(self) -> None:
        source = RUNTIME.read_text(encoding="utf-8")
        cmake = RUNTIME_CMAKE.read_text(encoding="utf-8")

        self.assertIn('#include "xiaotai_ai_protocol.h"', source)
        self.assertIn("xiaotai_ai_end_drain_begin", source)
        self.assertIn("xiaotai_ai_end_drain_step", source)
        self.assertIn("service_ai_end_drain();", source)
        self.assertIn("xiaotai_ai_protocol.c", cmake)

        end_branch = source.split(
            'strcmp(method->valuestring, "end_session") == 0', 1
        )[1].split("} else {", 1)[0]
        self.assertIn("begin_ai_end_drain", end_branch)
        self.assertNotIn("finish_session", end_branch)

    def test_p4_media_exposes_real_playback_drain_state(self) -> None:
        header = MEDIA_HEADER.read_text(encoding="utf-8")
        source = P4_MEDIA.read_text(encoding="utf-8")

        self.assertIn("audio_playback_pending", header)
        self.assertIn("audio_playback_active", header)
        self.assertIn("s_audio_playback_active", source)
        self.assertIn("uxQueueMessagesWaiting(s_audio_rx_ready_queue)", source)


if __name__ == "__main__":
    unittest.main()
