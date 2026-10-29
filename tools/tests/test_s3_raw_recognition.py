"""Exercise the LCKFB S3 recognition capture seam with distinct mic/ref PCM."""
import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
MEDIA = ROOT / "firmware/esp-idf/esp32s3/lckfb-esp32s3-xiaotai/components/starter_media"


class RawRecognitionCaptureTest(unittest.TestCase):
    def test_raw_microphone_drives_detection_and_recording(self):
        source = r'''
#include "starter_raw_recognition.h"
#include "xiaotai_recognition.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

int main(void) {
    starter_raw_recognition_t activity = {0};
    xiaotai_recognition_t recorder;
    int16_t *recording = calloc(XIAOTAI_RECOGNITION_SAMPLES, sizeof(int16_t));
    assert(recording);
    xiaotai_recognition_init(&recorder, recording);
    int16_t capture[640], raw[320];
    /* A loud playback reference alone must never trigger recognition. */
    for (int frame = 0; frame < 250; ++frame) {
        for (int i = 0; i < 320; ++i) {
            capture[2*i] = 0;
            capture[2*i+1] = 12000;
        }
        bool sound = starter_raw_recognition_feed(&activity, capture, 320, 2, 0,
                                                    raw, 320, true, 1000+frame*20);
        assert(!sound);
        xiaotai_recognition_feed(&recorder, raw, 320, sound, true, 1000+frame*20);
    }
    assert(recorder.pre_count == XIAOTAI_RECOGNITION_PRE_SAMPLES);
    for (int frame = 0; frame < 30; ++frame) {
        for (int i = 0; i < 320; ++i) {
            capture[2*i] = 1000; /* raw microphone; AEC-clean may be nearly silent */
            capture[2*i+1] = 12000;
        }
        bool sound = starter_raw_recognition_feed(&activity, capture, 320, 2, 0,
                                                    raw, 320, true, 6000+frame*20);
        for (int i = 0; i < 320; ++i) assert(raw[i] == 1000);
        xiaotai_recognition_feed(&recorder, raw, 320, sound, true, 6000+frame*20);
    }
    assert(recorder.recording);
    assert(recording[0] == 0);
    assert(recording[80000] == 1000);
    /* A local prompt/session boundary resets the detector and recorder. */
    bool sound = starter_raw_recognition_feed(&activity, capture, 320, 2, 0,
                                                raw, 320, false, 6600);
    xiaotai_recognition_feed(&recorder, raw, 320, sound, false, 6600);
    assert(!sound && !recorder.recording && !recorder.pre_count);
    sound = starter_raw_recognition_feed(&activity, capture, 320, 2, 0,
                                          raw, 320, true, 6620);
    assert(!sound); /* no stale 300 ms trigger across the boundary */
    assert(!starter_raw_recognition_feed(&activity, capture, 320, 2, 2,
                                          raw, 320, true, 6640));
    free(recording);
}
'''
        with tempfile.TemporaryDirectory() as temp:
            temp = pathlib.Path(temp)
            program = temp / "raw_recognition"
            source_path = temp / "test.c"
            source_path.write_text(source)
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I" + str(MEDIA / "include"),
                "-I" + str(ROOT / "platforms/esp-idf/components/starter_media_common/include"),
                "-I" + str(ROOT / "product/include"),
                str(source_path), str(MEDIA / "src/starter_raw_recognition.c"),
                str(ROOT / "product/src/xiaotai_recognition.c"),
                "-o", str(program),
            ], check=True)
            subprocess.run([str(program)], check=True)

    def test_lckfb_capture_wiring_preserves_aec_call_and_wake_feed(self):
        source = (MEDIA / "src/starter_media.c").read_text()
        for call in ("starter_raw_recognition_feed(&raw_recognition",
                     "starter_media_observe_pcm(s_recognition_raw_pcm",
                     "starter_aec_process_capture(capture_bytes, &clean)",
                     "starter_voice_feed_pcm16k(clean.pcm_16k"):
            self.assertTrue(call in source, call)
        m5 = (ROOT / "firmware/esp-idf/esp32s3/m5stack-cores3-lite/components/starter_media/CMakeLists.txt").read_text()
        self.assertNotIn("starter_raw_recognition.c", m5)
        self.assertNotIn("XIAOTAI_S3_RAW_RECOGNITION", m5)
        lckfb = (MEDIA / "CMakeLists.txt").read_text()
        self.assertIn("XIAOTAI_S3_RAW_RECOGNITION=1", lckfb)


if __name__ == "__main__":
    unittest.main()
