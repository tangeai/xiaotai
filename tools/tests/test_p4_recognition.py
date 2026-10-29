"""P4 recognition admission, physical microphone tap and UI rollout."""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
P4 = ROOT/'firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c'

class P4RecognitionTest(unittest.TestCase):
    def test_capture_uses_swapped_microphone_before_aec(self):
        source=(P4/'components/starter_media/src/starter_media.c').read_text()
        tap=source.index('starter_raw_recognition_feed(&raw_recognition')
        self.assertLess(source.index('capture[i * 2] = capture[i * 2 + 1]'), tap)
        self.assertLess(tap, source.index('starter_aec_process_capture(capture_bytes, &clean)'))
        self.assertIn('capture, capture_frames, 2, 0, s_recognition_raw_pcm', source)
        self.assertIn('starter_media_observe_pcm(s_recognition_raw_pcm, capture_frames', source)
        self.assertIn('starter_voice_feed_pcm16k(clean.pcm_16k', source)
        self.assertIn('atomic_store(&s_prompt_active, true)', source)
        self.assertIn('atomic_store(&s_prompt_guard_ms, esp_timer_get_time() / 1000 + 1000)', source)
        self.assertIn('captured_ms >= atomic_load(&s_prompt_guard_ms)', source)

    def test_runtime_and_home_switch_are_enabled_for_p4(self):
        source=(ROOT/'platforms/esp-idf/components/starter_runtime/src/starter_runtime.c').read_text()
        self.assertIn('#elif CONFIG_IDF_TARGET_ESP32P4\n    starter_recognition_init();', source)
        cmake=(P4/'components/starter_product/CMakeLists.txt').read_text()
        self.assertIn('XIAOTAI_RECOGNITION_HOME_SWITCH=1', cmake)
        self.assertIn('if(NOT CMAKE_BUILD_EARLY_EXPANSION)', cmake)
        self.assertIn('set(PROJECT_VER "1.1.0")', (P4/'CMakeLists.txt').read_text())
        p4_profile=source.split('#if CONFIG_IDF_TARGET_ESP32P4\n    static const char profile[] =')[1].split('#else')[0]
        self.assertIn('\\"firmware_version\\":\\"1.1.0\\"', p4_profile)

if __name__ == '__main__':
    unittest.main()
