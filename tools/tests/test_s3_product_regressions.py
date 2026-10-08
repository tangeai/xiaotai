"""Replay production ESP adapter functions against SDK boundary fakes."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
MEDIA = ROOT / "firmware/esp-idf/esp32s3/lckfb-esp32s3-xiaotai/components/starter_media/src/starter_media.c"
BOARD = ROOT / "boards/lckfb/esp32s3/board_adapter.c"

def function(source, name):
    match = re.search(r"(?:static\s+)?[\w *]+\b" + name + r"\([^;]*?\)\s*\{", source)
    if not match:
        raise AssertionError("missing production function: " + name)
    depth = 1
    end = match.end()
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]

def run_c(code):
    with tempfile.TemporaryDirectory(prefix="s3-product-") as tmp:
        path = Path(tmp)
        (path / "test.c").write_text(code)
        subprocess.run(["cc", "-std=c11", "-Wall", "-Werror", "-Wno-unused-function",
                        str(path / "test.c"), "-o", str(path / "test")], check=True)
        subprocess.run([str(path / "test")], check=True)

class S3ProductRegressions(unittest.TestCase):
    def test_room_focus_closes_media_and_invalidates_old_tokens(self):
        source = (ROOT / "platforms/esp-idf/components/starter_runtime/src/starter_runtime.c").read_text()
        focus = function(source, "room_set_foreground")
        current = function(source, "room_token_is_current")
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
static bool s_room_page_active, s_room_resume_pending, s_room_ptt;
static uint32_t s_room_page_epoch, s_room_token_epoch;
static int stops;
static void room_stop_connection(const char *presence, int error) {
    (void)presence; (void)error; stops++;
}
''' + focus + current + r'''
int main(void) {
    assert(!room_token_is_current());
    room_set_foreground(true);
    s_room_token_epoch=s_room_page_epoch;
    assert(room_token_is_current());
    room_set_foreground(true); assert(room_token_is_current());
    s_room_ptt=true; s_room_resume_pending=true;
    room_set_foreground(false);
    assert(stops==1 && !s_room_ptt && !s_room_resume_pending);
    assert(!room_token_is_current());
    room_set_foreground(false); assert(stops==1);
    room_set_foreground(true); assert(!room_token_is_current());
    s_room_token_epoch=s_room_page_epoch; assert(room_token_is_current());
}
''')

    def test_room_leave_requires_confirmation_and_cancel_keeps_room(self):
        source = (ROOT / "platforms/esp-idf/components/starter_product/src/starter_product.c").read_text()
        start = source.index("action == ACTION_ROOM_LEAVE)")
        end = source.index("} else if (action >= ACTION_ROOM_DIGIT_BASE", start)
        branch = "if (" + source[start:end] + "}"
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#define ACTION_ROOM_LEAVE 1
#define ACTION_ROOM_LEAVE_CONFIRM 2
#define ACTION_ROOM_LEAVE_CANCEL 3
#define PAGE_ROOM 1
#define PAGE_ROOM_LEAVE_CONFIRM 2
#define ESP_OK 0
static int s_page=PAGE_ROOM, leaves;
static char s_voice_feedback[64];
static int room_action_submitted(int err) {return err==0;}
static int starter_runtime_room_leave(void) { leaves++; return 0; }
static int64_t monotonic_ms(void) {return 0;}
static void set_voice_feedback(const char *s,int64_t now) {(void)s;(void)now;}
static void act(int action) {
''' + branch + r'''
}
int main(void) {
    act(ACTION_ROOM_LEAVE); assert(s_page==PAGE_ROOM_LEAVE_CONFIRM && leaves==0);
    act(ACTION_ROOM_LEAVE_CANCEL); assert(s_page==PAGE_ROOM && leaves==0);
    act(ACTION_ROOM_LEAVE_CONFIRM); assert(leaves==0);
    act(ACTION_ROOM_LEAVE);
    act(ACTION_ROOM_LEAVE_CONFIRM); assert(leaves==1 && s_page==PAGE_ROOM);
    act(ACTION_ROOM_LEAVE_CONFIRM); assert(leaves==1);
}
''')

    def test_room_members_are_paged_without_hiding_online_total(self):
        from test_s3_room_refresh import run_room_case
        run_room_case(r'''
 snapshot.room_member_count=8;
 for(int i=0;i<8;i++) snprintf(snapshot.room_members[i].id,65,"member%d",i);
 for(int page=0;page<3;page++) {
   s_room_members_page=page;product=snapshot;refresh_tick();
   assert(shown("在线 100 人"));
   for(int i=0;i<8;i++) {char id[16];snprintf(id,16,"member%d",i);
     assert(shown(id)==(i/3==page));}
 }
 snapshot.room_member_count=1;s_room_members_page=2;product=snapshot;refresh_tick();
 assert(s_room_members_page==0 && shown("member0"));
 snapshot.room_member_count=0;product=snapshot;refresh_tick();assert(shown("等待成员同步"));
 assert(nodes==initial_nodes);
''')

    def test_room_navigation_and_pending_call_overlay(self):
        source = (ROOT / "platforms/esp-idf/components/starter_product/src/starter_product.c").read_text()
        sync = function(source, "sync_room_foreground")
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#define PAGE_ROOM 1
#define PAGE_ROOM_CODE 2
#define PAGE_ROOM_PASSWORD 3
#define PAGE_ROOM_LEAVE_CONFIRM 4
#define PAGE_CALL 5
#define ESP_OK 0
static int s_page, events, error;
static bool s_room_ui_active, incoming, posted;
typedef struct {bool call_incoming;} snapshot_t;
static snapshot_t starter_runtime_product_snapshot(void) {
    return (snapshot_t){incoming};
}
static int starter_runtime_room_set_foreground(bool active) {events++;posted=active;return error;}
''' + sync + r'''
int main(void) {
    s_page=PAGE_ROOM; sync_room_foreground(); assert(posted && events==1);
    s_page=PAGE_ROOM_LEAVE_CONFIRM;sync_room_foreground();assert(events==1);
    s_page=PAGE_CALL;incoming=true;sync_room_foreground();assert(events==1 && s_room_ui_active);
    incoming=false;sync_room_foreground();assert(events==2 && !posted && !s_room_ui_active);
    s_page=PAGE_ROOM;sync_room_foreground();assert(events==3 && posted);
    error=1;s_page=0;sync_room_foreground();assert(s_room_ui_active);
    error=0;sync_room_foreground();assert(!s_room_ui_active && !posted);
}
''')

    def test_failed_audio_settings_preserve_display_and_persistence(self):
        source = (ROOT / "platforms/esp-idf/components/starter_product/src/starter_product.c").read_text()
        start = source.index("action == ACTION_VOLUME_DOWN &&")
        end = source.index("} else if (action == ACTION_SLEEP)", start)
        branch = "if (" + source[start:end] + "}"
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define ESP_OK 0
#define ESP_LOGW(...) ((void)0)
#define ACTION_VOLUME_DOWN 1
#define ACTION_VOLUME_UP 2
#define ACTION_SPEAKER_MUTE 3
#define ACTION_MIC_MUTE 4
#define ACTION_MIC_SENSITIVITY_DOWN 5
#define ACTION_MIC_SENSITIVITY_UP 6
static struct {uint8_t volume,microphone_sensitivity;bool speaker_muted,microphone_muted;} s_preferences={7,4,false,false};
static unsigned saves,calls;
static int error;
static int starter_media_set_speaker_volume(uint8_t value) {(void)value;calls++;return error;}
static int starter_media_set_speaker_muted(bool value) {(void)value;calls++;return error;}
static int starter_media_set_microphone_sensitivity(uint8_t value) {(void)value;calls++;return error;}
static void starter_media_set_microphone_muted(bool value) {(void)value;}
static void preferences_save(void) {saves++;}
static void action(int action) {
''' + branch + r'''
}
int main(void) {
 error=-1;
 action(ACTION_VOLUME_UP);action(ACTION_VOLUME_DOWN);action(ACTION_SPEAKER_MUTE);
 action(ACTION_MIC_SENSITIVITY_UP);action(ACTION_MIC_SENSITIVITY_DOWN);
 assert(s_preferences.volume==7 && s_preferences.microphone_sensitivity==4);
 assert(!s_preferences.speaker_muted && saves==0 && calls==5);
 error=0;
 action(ACTION_MIC_SENSITIVITY_UP);assert(s_preferences.microphone_sensitivity==5 && saves==1);
 action(ACTION_MIC_SENSITIVITY_UP);assert(saves==1 && calls==6);
 action(ACTION_MIC_SENSITIVITY_DOWN);assert(s_preferences.microphone_sensitivity==4 && saves==2);
 action(ACTION_VOLUME_UP);assert(s_preferences.volume==8 && saves==3);
 action(ACTION_VOLUME_DOWN);assert(s_preferences.volume==7 && saves==4);
 action(ACTION_SPEAKER_MUTE);assert(s_preferences.speaker_muted && saves==5);
}
''')

    def test_microphone_sensitivity_changes_only_near_end_gain(self):
        board = function(BOARD.read_text(), "audio_adapter_set_capture_gain")
        media = function(MEDIA.read_text(), "starter_media_set_microphone_sensitivity")
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NOT_SUPPORTED 3
#define ESP_CODEC_DEV_OK 0
#define ESP_CODEC_DEV_MAKE_CHANNEL_MASK(n) (1U << (n))
#define ESP_LOGI(...) ((void)0)
static void *s_microphone_dev = (void *)1;
static float gain;
static unsigned mask, calls;
static int error;
static int esp_codec_dev_set_in_channel_gain(void *dev, unsigned channel, float db) {
    assert(dev == s_microphone_dev); gain=db; mask=channel; calls++; return error;
}
''' + board + r'''
static bool ready(void *ctx) { return ctx != NULL; }
static struct {void *context; bool (*ready)(void *); int (*set_capture_gain)(void *, unsigned);} adapter = {
    (void *)1, ready, audio_adapter_set_capture_gain
};
'''+ r'''
static __typeof__(adapter) *s_audio_adapter = &adapter;
static atomic_uchar s_microphone_sensitivity = 4;
''' + media + r'''
int main(void) {
    const float expected[] = {18,24,30,33,36};
    assert(starter_media_set_microphone_sensitivity(0) == ESP_ERR_INVALID_ARG);
    assert(starter_media_set_microphone_sensitivity(6) == ESP_ERR_INVALID_ARG);
    assert(calls == 0);
    for (unsigned i=1;i<=5;i++) {
        assert(starter_media_set_microphone_sensitivity(i)==ESP_OK);
        assert(gain==expected[i-1] && mask==1U);
        assert(atomic_load(&s_microphone_sensitivity)==i);
    }
    error=-1;
    assert(starter_media_set_microphone_sensitivity(1)==ESP_FAIL);
    assert(atomic_load(&s_microphone_sensitivity)==5);
    s_audio_adapter=NULL;
    assert(starter_media_set_microphone_sensitivity(1)!=ESP_OK);
}
''')

if __name__ == "__main__":
    unittest.main()
