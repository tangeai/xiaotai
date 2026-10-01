import hashlib
import json
import struct
import unittest
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]
ASSET_ROOT = REPO / "product" / "assets" / "expressions"
CANONICAL_EMOTIONS = [
    "neutral",
    "happy",
    "laughing",
    "funny",
    "sad",
    "angry",
    "crying",
    "loving",
    "embarrassed",
    "surprised",
    "shocked",
    "thinking",
    "winking",
    "cool",
    "relaxed",
    "delicious",
    "kissy",
    "confident",
    "sleepy",
    "silly",
    "confused",
]

CONCEPT_SHEET_EMOTIONS = [
    "neutral", "happy", "surprised", "winking", "silly", "cool",
    "sad", "angry", "crying",
]


class ExpressionAssetTest(unittest.TestCase):
    def test_candidate_themes_are_complete_and_traceable(self):
        manifest = json.loads((ASSET_ROOT / "manifest.json").read_text())
        self.assertEqual(manifest["schema_version"], 1)
        self.assertEqual(manifest["canonical_emotions"], CANONICAL_EMOTIONS)
        self.assertEqual(len(manifest["themes"]), 2)

        theme_ids = set()
        for theme in manifest["themes"]:
            theme_ids.add(theme["id"])
            self.assertEqual(theme["status"], "concept-sheet")
            self.assertFalse(theme["runtime_ready"])
            self.assertFalse(theme["animated"])
            self.assertEqual(theme["emotion_order"], CONCEPT_SHEET_EMOTIONS)
            self.assertEqual(theme["coverage"], "partial")

            image = ASSET_ROOT / theme["contact_sheet"]
            data = image.read_bytes()
            self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
            width, height = struct.unpack(">II", data[16:24])
            self.assertEqual([width, height], theme["pixel_size"])
            self.assertEqual(hashlib.sha256(data).hexdigest(), theme["sha256"])

        self.assertEqual(
            theme_ids,
            {"pixel-robot-glossy-v1", "pixel-robot-compact-v1"},
        )


if __name__ == "__main__":
    unittest.main()
