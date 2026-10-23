"""Exercise the shared call-media decision used by P4 and S3 entry points."""

import re
import unittest

from test_s3_product_regressions import ROOT, function, run_c


RUNTIME = ROOT / "platforms/esp-idf/components/starter_runtime/src/starter_runtime.c"
PRODUCT_UI = ROOT / "platforms/esp-idf/components/starter_product/src/starter_product.c"
BK_PROTOCOL = ROOT / "product/src/xiaotai_call_protocol.c"


class DefaultVideoCall(unittest.TestCase):
    def test_video_capability_and_explicit_voice_request(self):
        source = RUNTIME.read_text()
        selector = function(source, "call_video_for_request")
        for p4, expected in ((1, 1), (0, 0)):
            run_c(f'''
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#define CONFIG_IDF_TARGET_ESP32P4 {p4}
{selector}
int main(void) {{
    assert(call_video_for_request(NULL, true) == {p4});
    assert(call_video_for_request("", true) == {p4});
    assert(call_video_for_request(NULL, false) == false);
    assert(call_video_for_request("video", false) == {expected});
    assert(call_video_for_request("voice", true) == false);
    assert(call_video_for_request("audio", true) == false);
}}
''')

    def test_home_key_ai_and_refreshed_contacts_use_media_policy(self):
        source = RUNTIME.read_text()
        self.assertIn("dial_contact(index, call_video_for_request(NULL, true));", source)
        self.assertIn("handle_wechat_quick_call();", function(source, "dial_first_contact"))
        self.assertIn("dial_contact(contact_index, call_video_for_request(media, wechat));", source)
        self.assertRegex(source, r'bool media_valid\s*=.*"video"')

    def test_bk_audio_only_encoder_remains_audio(self):
        source = BK_PROTOCOL.read_text()
        self.assertIn('\\"call_type\\":\\"audio\\"', source)
        self.assertIn('\\"wx_room_type\\":\\"voice\\"', source)

    def test_home_button_shows_qr_only_when_no_wechat_contact(self):
        handler = function(PRODUCT_UI.read_text(), "start_home_wechat_quick_call")
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#define ESP_OK 0
typedef int esp_err_t;
typedef struct {uint8_t wechat_contact_count;} starter_runtime_product_snapshot_t;
enum {PAGE_HOME_FACE, PAGE_WECHAT_QR};
static int s_page = PAGE_HOME_FACE, result, submitted, count;
static starter_runtime_product_snapshot_t starter_runtime_product_snapshot(void) {
    return (starter_runtime_product_snapshot_t){.wechat_contact_count=count};
}
static esp_err_t starter_runtime_wechat_quick_call(void) {submitted++; return result;}
''' + handler + r'''
int main(void) {
    count=1; start_home_wechat_quick_call();
    assert(submitted==1 && s_page==PAGE_HOME_FACE);
    count=0; start_home_wechat_quick_call();
    assert(submitted==2 && s_page==PAGE_WECHAT_QR);
    s_page=PAGE_HOME_FACE; result=-1;
    start_home_wechat_quick_call();
    assert(submitted==3 && s_page==PAGE_HOME_FACE);
}
''')


if __name__ == "__main__":
    unittest.main()
