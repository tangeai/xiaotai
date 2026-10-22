"""BK button logs distinguish gestures, user actions, and failures."""

import unittest

from test_s3_product_regressions import ROOT, function


PROJECT = ROOT / 'firmware/beken/bk7258/lckfb-bk7258-xiaotai'


class BKButtonLogLevels(unittest.TestCase):
    def test_expected_gestures_and_busy_ignores_are_debug(self):
        button = (ROOT / 'platforms/beken/runtime/src/xiaotai_button.c').read_text()
        app = (ROOT / 'platforms/beken/runtime/src/xiaotai_app.c').read_text()
        task = function(button, 'button_task')
        intent = function(app, 'handle_intent')
        for message in ('KEY short press', 'KEY double click', 'KEY long press'):
            self.assertIn(f'BK_LOGD(TAG, "{message}', task)
        for message in ('KEY double click ignored: group room active',
                        'KEY double click ignored: media owner busy',
                        'AI button ignored: runtime not ready'):
            self.assertIn(f'BK_LOGD(TAG, "{message}', intent)

    def test_accepted_actions_and_delivery_fault_have_meaningful_levels(self):
        app = (ROOT / 'platforms/beken/runtime/src/xiaotai_app.c').read_text()
        callback = function(app, 'button_pressed')
        intent = function(app, 'handle_intent')
        self.assertIn('BK_LOGW(TAG, "button event dropped', callback)
        for message in ('KEY accepted device call',
                        'KEY requested device call hangup',
                        'button requested AI stop'):
            self.assertIn(f'BK_LOGI(TAG, "{message}', intent)

    def test_periodic_media_diagnostics_are_debug(self):
        audio = (ROOT / 'platforms/beken/runtime/src/xiaotai_audio.c').read_text()
        rtc = (ROOT / 'platforms/beken/runtime/src/xiaotai_tirtc.c').read_text()
        self.assertIn('BK_LOGD(TAG,\n            "ECHO_DIAG AEC', audio)
        self.assertIn('BK_LOGD(TAG,\n                    "SDK AGC level', audio)
        self.assertIn('BK_LOGD(TAG,\n                        "SDK NS frames', audio)
        self.assertIn('BK_LOGD(TAG,\n                "ECHO_DIAG RX-SUM', rtc)
        self.assertIn('xiaotai_log_write(unexpected ? XIAOTAI_LOG_WARN : '
                      'XIAOTAI_LOG_DEBUG,', rtc)

    def test_speaker_close_trace_is_debug(self):
        patch = (ROOT / 'platforms/beken/tools/patches/bk-avdk-speaker-close-diagnostics.patch').read_text()
        self.assertNotIn('+    LOGI("[DEBUG-bk-spk-close]', patch)
        self.assertIn('+    LOGD("[DEBUG-bk-spk-close]', patch)


if __name__ == '__main__':
    unittest.main()
